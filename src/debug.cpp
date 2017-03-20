/******************************* libjwdpmi **********************************
Copyright (C) 2016-2017  J.W. Jagersma

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <array>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <jw/dpmi/fpu.h>
#include <jw/dpmi/dpmi.h>
#include <jw/dpmi/debug.h>
#include <jw/dpmi/cpu_exception.h>
#include <jw/io/rs232.h>
#include <jw/alloc.h>

// TODO: terminate_handler and trap SIGABRT

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            bool gdb_interface_setup { false };
        }

        namespace gdb
        {
            const bool debugmsg = true;

            locked_pool_allocator<> alloc { 1_MB };
            std::deque<std::string, locked_pool_allocator<>> sent { alloc };
            //std::unordered_map<std::string, std::string, std::hash<std::string>, std::equal_to<std::string>, locked_pool_allocator<>> supported { alloc };
            std::map<std::string, std::string, std::less<std::string>, locked_pool_allocator<>> supported { alloc };
            std::map<std::uintptr_t, watchpoint, std::less<std::uintptr_t>, locked_pool_allocator<>> watchpoints { alloc };
            std::map<std::uintptr_t, byte, std::less<std::uintptr_t>, locked_pool_allocator<>> breakpoints { alloc };

            std::array<std::unique_ptr<exception_handler>, 0x20> exception_handlers;
            auto gdb { init_unique<std::iostream>(alloc) };

            std::uintptr_t last_eip { };
            bool trace { false };
            char last_p { '?' };
            new_exception_frame last_exception_frame;
            cpu_registers last_exception_registers;
            exception_num last_exception;
            bool reentry { false };

            enum regnum
            {
                eax, ecx, edx, ebx,
                esp, ebp, esi, edi,
                eip, eflags,
                cs, ss, ds, es, fs, gs,
                st0, st1, st2, st3, st4, st5, st6, st7,
                fctrl, cstat, ftag, fiseg, fioff, foseg, fooff, fop,
                xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7,
                mxcsr
            };
            regnum& operator++(regnum& r) { return r = static_cast<regnum>(r + 1); }

            const std::array<std::size_t, 40> reglen
            {
                4, 4, 4, 4,
                4, 4, 4, 4,
                4, 4,
                2, 2, 2, 2, 2, 2,
                10, 10, 10, 10, 10, 10, 10, 10
                // TODO: fpu registers
            };

            inline auto signal_number(exception_num exc)
            {
                switch (exc)
                {
                case 0x00: return 0x08; // SIGFPE
                case 0x01: return 0x05; // SIGTRAP
                case 0x02: return 0x09; // SIGKILL
                case 0x03: return 0x05; // SIGTRAP
                case 0x04: return 0x08; // SIGFPE
                case 0x05: return 0x0b; // SIGSEGV
                case 0x06: return 0x04; // SIGILL
                case 0x07: return 0x08; // SIGFPE
                case 0x08: return 0x09; // SIGKILL
                case 0x09: return 0x0b; // SIGSEGV
                case 0x0a: return 0x0b; // SIGSEGV
                case 0x0b: return 0x0b; // SIGSEGV
                case 0x0c: return 0x0b; // SIGSEGV
                case 0x0d: return 0x0b; // SIGSEGV
                case 0x0e: return 0x0b; // SIGSEGV
                case 0x10: return 0x07; // SIGEMT
                case 0x11: return 0x0a; // SIGBUS
                case 0x12: return 0x09; // SIGKILL
                case 0x13: return 0x08; // SIGFPE
                default: return 143;
                }
            }

            inline auto decode(const std::string& s)
            {
                return std::stoul(s, nullptr, 16);
            }

            template <typename T>
            bool reverse_decode(const std::string& in, T* out, std::size_t len = sizeof(T))
            {
                if (in.size() < 2 * len) return false;
                auto ptr = reinterpret_cast<byte*>(out);
                for (std::size_t i = 0; i < len; ++i)
                {
                    auto l = i * 2 + 2 >= in.size() ? in.npos : 2;
                    ptr[i] = decode(in.substr(i * 2, l));
                }
                return true;
            }

            template <typename T>
            void encode(std::ostream& out, T* in, std::size_t len = sizeof(T))
            {
                auto ptr = reinterpret_cast<const byte*>(in);
                for (std::size_t i = 0; i < len; ++i) 
                    out << std::setw(2) << static_cast<std::uint32_t>(ptr[i]);
            }

            void encode_null(std::ostream& out, std::size_t len)
            {
                for (std::size_t i = 0; i < len; ++i) 
                    out << "xx";
            }

            std::uint32_t checksum(const std::string& s)
            {
                std::uint8_t r { 0 };
                for (auto c : s) r += c;
                return r;
            }

            void send_notification(const std::string& output)
            {
                if (debugmsg) std::clog << "send --> \"" << output << "\"\n";
                const auto sum = checksum(output);
                *gdb << '%' << output << '#' << std::setfill('0') << std::hex << std::setw(2) << sum << std::flush;
            }
            
            void send_packet(const std::string& output)
            {
                if (debugmsg) std::clog << "send --> \"" << output << "\"\n";
                const auto sum = checksum(output);
                *gdb << '$' << output << '#' << std::setfill('0') << std::hex << std::setw(2) << sum << std::flush;
                sent.push_back(output);
            }

            auto recv_packet()
            {
            retry:
                switch (gdb->get())
                {
                case '-': send_packet(sent.front());
                case '+': if (sent.size() > 0) sent.pop_front();
                default: goto retry;
                case '$': break;
                }

                std::string input;
                std::getline(*gdb, input, '#');
                std::string sum;
                sum += gdb->get();
                sum += gdb->get();
                if (decode(sum) == checksum(input)) *gdb << '+' << std::flush;
                else { *gdb << '-' << std::flush; goto retry; }
                if (debugmsg) std::clog << "recv <-- \""<< input << "\"\n";

                std::deque<std::string> parsed_input { };
                std::size_t pos { 1 };
                parsed_input.push_back(input.substr(0, 1));
                while (pos < input.size())
                {
                    auto p = std::min({ input.find(',', pos), input.find(':', pos), input.find(';', pos), input.find('=', pos) });
                    if (p == input.npos) p = input.size();
                    parsed_input.push_back(input.substr(pos, p - pos));
                    pos += p - pos + 1;
                }
                return parsed_input;
            }

            void reg(std::ostream& out, regnum r, cpu_registers* reg, exception_frame* frame, bool new_type)
            {
                auto* new_frame = static_cast<new_exception_frame*>(frame);
                switch (r)
                {
                case eax: encode(out, &reg->eax); return;
                case ebx: encode(out, &reg->ebx); return;
                case ecx: encode(out, &reg->ecx); return;
                case edx: encode(out, &reg->edx); return;
                case ebp: encode(out, &reg->ebp); return;
                case esi: encode(out, &reg->esi); return;
                case edi: encode(out, &reg->edi); return;
                case esp: encode(out, &frame->stack.offset); return;
                case eflags: encode(out, &frame->flags.raw_eflags); return;
                case cs: encode(out, &frame->fault_address.segment); return;
                case ss: encode(out, &frame->stack.segment); return;
                case ds: if (new_type) encode(out, &new_frame->ds); else encode_null(out, reglen[r]); return;
                case es: if (new_type) encode(out, &new_frame->es); else encode_null(out, reglen[r]); return;
                case fs: if (new_type) encode(out, &new_frame->fs); else encode_null(out, reglen[r]); return;
                case gs: if (new_type) encode(out, &new_frame->gs); else encode_null(out, reglen[r]); return;
                case eip:
                {
                    auto eip = frame->fault_address.offset;
                    if (last_exception == 0x01) eip = last_eip;
                    //else if (last_exception == 0x03) eip -= 1;
                    encode(out, &eip);
                    return;
                }
                default: if (r > mxcsr) return;
                    auto fpu = detail::fpu_context_switcher.get_last_context();
                    switch (r)
                    {
                    default:; // TODO
                    }
                }
            }

            bool setreg(regnum r, const std::string& value, cpu_registers* reg, exception_frame* frame, bool new_type)
            {
                auto* new_frame = static_cast<new_exception_frame*>(frame);
                switch (r)
                {
                case eax:    return reverse_decode(value, &reg->eax, reglen[r]);
                case ebx:    return reverse_decode(value, &reg->ebx, reglen[r]);
                case ecx:    return reverse_decode(value, &reg->ecx, reglen[r]);
                case edx:    return reverse_decode(value, &reg->edx, reglen[r]);
                case ebp:    return reverse_decode(value, &reg->ebp, reglen[r]);
                case esi:    return reverse_decode(value, &reg->esi, reglen[r]);
                case edi:    return reverse_decode(value, &reg->edi, reglen[r]);
                case esp:    return reverse_decode(value, &frame->stack.offset, reglen[r]);
                case eip:    return reverse_decode(value, &frame->fault_address.offset, reglen[r]);
                case eflags: return reverse_decode(value, &frame->flags.raw_eflags, reglen[r]);
                case cs:     return reverse_decode(value, &frame->fault_address.segment, reglen[r]);
                case ss:     return reverse_decode(value, &frame->stack.segment, reglen[r]);
                case ds: if (new_type) { return reverse_decode(value, &new_frame->ds, reglen[r]); } return false;
                case es: if (new_type) { return reverse_decode(value, &new_frame->es, reglen[r]); } return false;
                case fs: if (new_type) { return reverse_decode(value, &new_frame->fs, reglen[r]); } return false;
                case gs: if (new_type) { return reverse_decode(value, &new_frame->gs, reglen[r]); } return false;
                default: if (r > mxcsr) return false;
                    auto fpu = detail::fpu_context_switcher.get_last_context();
                    switch (r)
                    {
                    default: return false; // TODO
                    }
                }
            }

            bool handle_packet(exception_num exc, cpu_registers* r, exception_frame* f, bool t)
            {
                using namespace std;
                std::deque<std::string> packet { "?" };
                while (true)
                {
                    std::stringstream s { };
                    s << hex << setfill('0');
                    if (!trace) packet = recv_packet();
                    auto& p = packet.front();
                    last_p = p[0];
                    if (p == "?")   // stop reason
                    {
                        if (exc == 1 || exc == 3)
                        {
                            s << "T" << setw(2) << signal_number(exc);
                            s << eflags << ':'; reg(s, eflags, r, f, t); s << ';';
                            s << eip << ':'; reg(s, eip, r, f, t); s << ';';
                            s << esp << ':'; reg(s, esp, r, f, t); s << ';';
                            std::pair<const std::uintptr_t, watchpoint>* watchpoint_hit { nullptr };
                            for (auto& w : watchpoints) if (w.second.get_state()) watchpoint_hit = &w;
                            if (watchpoint_hit != nullptr)
                            {
                                if (watchpoint_hit->second.get_type() == watchpoint::execute) s << "hwbreak:;";
                                else
                                {
                                    s << "watch:"; 
                                    encode(s, &watchpoint_hit->first); 
                                    s << ";";
                                }
                            }
                            else s << "swbreak:;";
                            send_packet(s.str());
                        }
                        else
                        {
                            s << "S" << setw(2) << signal_number(exc);
                            send_packet(s.str());
                        }
                        trace = false;
                    }
                    else if (p == "q")  // query
                    {
                        auto& q = packet[1];
                        if (q == "Supported")
                        {
                            packet.pop_front();
                            for (auto str : packet)
                            {
                                auto back = str.back();
                                auto equals_sign = str.find('=', 0);
                                if (back == '+' || back == '-')
                                {
                                    str.pop_back();
                                    supported[str] = back;
                                }
                                else if (equals_sign != str.npos)
                                {
                                    supported[str.substr(0, equals_sign)] = str.substr(equals_sign + 1);
                                }
                            }
                            send_packet("PacketSize=100000;swbreak+");
                        }
                        else if (q == "Attached") send_packet("0");
                        else send_packet("");
                    }
                    else if (p == "p")  // read one register
                    {
                        auto regn = static_cast<regnum>(decode(packet[1]));
                        reg(s, regn, r, f, t);
                        if (s.str().size() == 0) encode_null(s, reglen[regn]);
                        send_packet(s.str());
                    }
                    else if (p == "P")  // write one register
                    {
                        if (setreg(static_cast<regnum>(decode(packet[1])), packet[2], r, f, t)) send_packet("OK");
                        else send_packet("E00");
                    }
                    else if (p == "g")  // read registers
                    {
                        for (int i = eax; i <= eflags; ++i)
                            reg(s, static_cast<regnum>(i), r, f, t);
                        send_packet(s.str());
                    }
                    else if (p == "G")  // write registers
                    {
                        regnum reg { };
                        std::size_t pos { };
                        bool fail { false };
                        while (pos < packet[1].size())
                        {
                            if (fail |= setreg(reg, packet[1].substr(pos), r, f, t))
                            {
                                send_packet("E00");
                                break;
                            }
                            pos += reglen[reg] * 2;
                            ++reg;
                        }
                        if (!fail) send_packet("OK");
                    }
                    else if (p == "m")  // read memory
                    {
                        auto* addr = reinterpret_cast<byte*>(decode(packet[1]));
                        std::size_t len = decode(packet[2]);
                        encode(s, addr, len);
                        send_packet(s.str());
                    }
                    else if (p == "M")  // write memory
                    {
                        auto* addr = reinterpret_cast<byte*>(decode(packet[1]));
                        std::size_t len = decode(packet[2]);
                        if (reverse_decode(packet[3], addr, len)) send_packet("OK");
                        else send_packet("E00");
                    }
                    else if (p == "c")  // continue
                    {
                        if (packet.size() > 1) f->fault_address.offset = decode(packet[1]);
                        f->flags.trap = false;
                        return true;
                    }
                    else if (p == "s")  // step
                    {
                        if (packet.size() > 1) f->fault_address.offset = decode(packet[1]);
                        f->flags.trap = true;
                        return true;
                    }
                    else if (p == "C")  // continue with signal
                    {
                        if (packet.size() > 2) f->fault_address.offset = decode(packet[2]);
                        f->flags.trap = false;
                        return false;
                    }
                    else if (p == "S")  // step with signal
                    {
                        if (packet.size() > 2) f->fault_address.offset = decode(packet[2]);
                        f->flags.trap = true;
                        return false;
                    }
                    else if (p == "Z")  // set break/watchpoint
                    {
                        auto& z = packet[1];
                        std::uintptr_t addr = decode(packet[2]);
                        auto ptr = reinterpret_cast<byte*>(addr);
                        if (z == "0")   // set breakpoint
                        {
                            if (packet.size() > 4)  // conditional breakpoint
                            {
                                send_packet("");    // not implemented (TODO)
                                continue;
                            }
                            breakpoints.emplace(addr, *ptr);
                            *ptr = 0xcc;
                            send_packet("OK");
                        }
                        else            // set watchpoint
                        {
                            watchpoint::watchpoint_type w;
                            if (z == "1") w = watchpoint::execute;
                            else if (z == "2") w = watchpoint::read_write;
                            else if (z == "3") w = watchpoint::read;
                            else if (z == "4") w = watchpoint::read_write;
                            else
                            {
                                send_packet("");
                                continue;
                            }
                            try
                            {
                                std::size_t size = decode(packet[3]);
                                watchpoints.emplace(addr, watchpoint { ptr, w, size });
                                send_packet("OK");
                            }
                            catch (...)
                            {
                                send_packet("E00");
                            }
                        }
                    }
                    else if (p == "z")  // remove break/watchpoint
                    {
                        auto& z = packet[1];
                        std::uintptr_t addr = decode(packet[2]);
                        auto ptr = reinterpret_cast<byte*>(addr);
                        if (z == "0")   // remove breakpoint
                        {
                            if (breakpoints.count(addr))
                            {
                                *ptr = breakpoints[addr];
                                send_packet("OK");
                            }
                            else send_packet("E00");
                        }
                        else            // remove watchpoint
                        {
                            if (watchpoints.count(addr))
                            {
                                watchpoints.erase(addr);
                                send_packet("OK");
                            }
                            else send_packet("E00");
                        }
                    }
                    else if (p == "k") return false;    // kill (this is a stupid way to do it...)
                    else send_packet("");   // unknown packet
                }
            }

            bool handle_exception(exception_num exc, cpu_registers* r, exception_frame* f, bool t)
            {
                if (debugmsg) std::clog << "entering exception 0x" << std::hex << exc << "\n";
                if (debugmsg) std::clog << *static_cast<new_exception_frame*>(f) << *r;
                switch (last_p)
                {
                case 'c':
                case 's':
                case 'C':
                case 'S':
                case '?':
                    trace = true;
                    break;
                default:
                    trace = false;
                }
                if (reentry)
                {
                    if (exc == 0x01) return true;   // watchpoint trap, ignore
                    if (!trace) send_packet("EEE"); // last command caused another exception
                    last_exception_frame.info_bits.redirect_elsewhere = true;
                    detail::fpu_context_switcher.leave();
                    --detail::exception_count;      // pretend it never happened.
                }
                else
                {
                    last_exception_frame = { };
                    if (t) last_exception_frame = *static_cast<new_exception_frame*>(f);
                    else static_cast<old_exception_frame&>(last_exception_frame) = *f;
                    last_exception_registers = *r;
                    last_exception = exc; 
                    if (exc == 0x03) last_exception_frame.fault_address.offset -= 1;
                    send_notification("Stop");
                }
                reentry = true;

                bool result { false };
                try
                {
                    result = handle_packet(last_exception, &last_exception_registers, &last_exception_frame, t);
                    for (auto& w : watchpoints) w.second.reset();
                }
                catch (...) 
                { 
                    std::cerr << "Exception occured while communicating with GDB.\n"; 
                }
                reentry = false;

                if (t) *f = static_cast<new_exception_frame&>(last_exception_frame);
                else *static_cast<old_exception_frame*>(f) = last_exception_frame;
                *r = last_exception_registers;
                last_eip = last_exception_frame.fault_address.offset;

                if (debugmsg) std::clog << "leaving exception 0x" << std::hex << exc << "\n";
                return result;
            }

            void setup(const io::rs232_config& cfg)
            {
                if (jw::dpmi::detail::gdb_interface_setup) return;
                jw::dpmi::detail::gdb_interface_setup = true;

                gdb = allocate_unique<io::rs232_stream>(alloc, cfg);

                exception_handlers[0x00] = std::make_unique<exception_handler>(0x00, [](auto* r, auto* f, bool t) { return handle_exception(0x00, r, f, t); });
                exception_handlers[0x01] = std::make_unique<exception_handler>(0x01, [](auto* r, auto* f, bool t) { return handle_exception(0x01, r, f, t); });
                exception_handlers[0x02] = std::make_unique<exception_handler>(0x02, [](auto* r, auto* f, bool t) { return handle_exception(0x02, r, f, t); });
                exception_handlers[0x03] = std::make_unique<exception_handler>(0x03, [](auto* r, auto* f, bool t) { return handle_exception(0x03, r, f, t); });
                exception_handlers[0x04] = std::make_unique<exception_handler>(0x04, [](auto* r, auto* f, bool t) { return handle_exception(0x04, r, f, t); });
                exception_handlers[0x05] = std::make_unique<exception_handler>(0x05, [](auto* r, auto* f, bool t) { return handle_exception(0x05, r, f, t); });
                exception_handlers[0x06] = std::make_unique<exception_handler>(0x06, [](auto* r, auto* f, bool t) { return handle_exception(0x06, r, f, t); });
                exception_handlers[0x07] = std::make_unique<exception_handler>(0x07, [](auto* r, auto* f, bool t) { return handle_exception(0x07, r, f, t); });
                exception_handlers[0x08] = std::make_unique<exception_handler>(0x08, [](auto* r, auto* f, bool t) { return handle_exception(0x08, r, f, t); });
                exception_handlers[0x09] = std::make_unique<exception_handler>(0x09, [](auto* r, auto* f, bool t) { return handle_exception(0x09, r, f, t); });
                exception_handlers[0x0a] = std::make_unique<exception_handler>(0x0a, [](auto* r, auto* f, bool t) { return handle_exception(0x0a, r, f, t); });
                exception_handlers[0x0b] = std::make_unique<exception_handler>(0x0b, [](auto* r, auto* f, bool t) { return handle_exception(0x0b, r, f, t); });
                exception_handlers[0x0c] = std::make_unique<exception_handler>(0x0c, [](auto* r, auto* f, bool t) { return handle_exception(0x0c, r, f, t); });
                exception_handlers[0x0d] = std::make_unique<exception_handler>(0x0d, [](auto* r, auto* f, bool t) { return handle_exception(0x0d, r, f, t); });
                exception_handlers[0x0e] = std::make_unique<exception_handler>(0x0e, [](auto* r, auto* f, bool t) { return handle_exception(0x0e, r, f, t); });

                capabilities c { };
                if (!c.supported) return;
                if (std::strncmp(c.vendor_info.name, "HDPMI", 5) != 0) return;  // TODO: figure out if other hosts support these too
                if (!detail::test_cr0_access())
                    exception_handlers[0x10] = std::make_unique<exception_handler>(0x10, [](auto* r, auto* f, bool t) { return handle_exception(0x10, r, f, t); });
                exception_handlers[0x11] = std::make_unique<exception_handler>(0x11, [](auto* r, auto* f, bool t) { return handle_exception(0x11, r, f, t); });
                exception_handlers[0x12] = std::make_unique<exception_handler>(0x12, [](auto* r, auto* f, bool t) { return handle_exception(0x12, r, f, t); });
                exception_handlers[0x13] = std::make_unique<exception_handler>(0x13, [](auto* r, auto* f, bool t) { return handle_exception(0x13, r, f, t); });
                exception_handlers[0x14] = std::make_unique<exception_handler>(0x14, [](auto* r, auto* f, bool t) { return handle_exception(0x14, r, f, t); });
                exception_handlers[0x1e] = std::make_unique<exception_handler>(0x1e, [](auto* r, auto* f, bool t) { return handle_exception(0x1e, r, f, t); });
            }
        }

        namespace detail
        {
            void setup_gdb_interface(const io::rs232_config& cfg) { jw::dpmi::gdb::setup(cfg); }
        }

        bool debug() { return detail::gdb_interface_setup; }
    } 
}
