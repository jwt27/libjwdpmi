/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <array>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <memory>
#include <jw/dpmi/fpu.h>
#include <jw/dpmi/dpmi.h>
#include <jw/dpmi/debug.h>
#include <jw/dpmi/cpu_exception.h>
#include <jw/io/rs232.h>
#include <jw/alloc.h>
#include <../jwdpmi_config.h>

// TODO: terminate_handler and trap SIGABRT

namespace jw
{
    namespace dpmi
    {
    #ifndef NDEBUG
        namespace detail
        {
            const bool debugmsg = config::enable_gdb_debug_messages;

            locked_pool_allocator<> alloc { 1_MB };
            std::deque<std::string, locked_pool_allocator<>> sent { alloc };
            std::map<std::string, std::string, std::less<std::string>, locked_pool_allocator<>> supported { alloc };
            std::map<std::uintptr_t, watchpoint, std::less<std::uintptr_t>, locked_pool_allocator<>> watchpoints { alloc };
            std::map<std::uintptr_t, byte, std::less<std::uintptr_t>, locked_pool_allocator<>> breakpoints { alloc };

            std::array<std::unique_ptr<exception_handler>, 0x20> exception_handlers;
            std::unique_ptr<std::iostream, allocator_delete<jw::dpmi::locking_allocator<std::iostream>>> gdb;

            volatile bool debugger_reentry { false };

            struct packet_string : public std::string
            {
                char delim;
                template <typename T>
                packet_string(T&& str, char delimiter): std::string(std::forward<T>(str)), delim(delimiter) { }
                using std::string::operator=;
                using std::string::basic_string;
            };

            std::uint32_t current_thread_id { 1 };  
            std::uint32_t selected_thread_id { 1 };

            struct thread_info
            {
                std::weak_ptr<thread::detail::thread> thread;
                std::uintptr_t last_eip { };
                new_exception_frame frame;
                cpu_registers reg;
                exception_num last_exception;
                bool use_sigcont { false };
                std::uintptr_t step_range_begin { 0 };
                std::uintptr_t step_range_end { 0 };
                
                enum
                {
                    none,
                    stop,
                    cont,
                    step,
                    cont_sig,
                    step_sig,
                    step_range
                } action;

                void set_action(const auto& a, std::uintptr_t resume_at = 0, std::uintptr_t rbegin = 0, std::uintptr_t rend = 0)
                {
                    if (resume_at != 0) frame.fault_address.offset = resume_at;
                    auto t = thread.lock();
                    t->resume();
                    if (a[0] == 'c')  // continue
                    {
                        frame.flags.trap = false;
                        action = cont;
                    }
                    else if (a[0] == 's')  // step
                    {
                        frame.flags.trap = true;
                        thread::detail::thread_details::set_trap(t);
                        action = step;
                    }
                    else if (a[0] == 'C')  // continue with signal
                    {
                        frame.flags.trap = false;
                        if (a.substr(1) == "13") action = cont; // SIGCONT
                        else action = cont_sig;
                    }
                    else if (a[0] == 'S')  // step with signal
                    {
                        frame.flags.trap = true;
                        thread::detail::thread_details::set_trap(t);
                        if (a.substr(1) == "13") action = step; // SIGCONT
                        else action = step_sig;
                    }
                    else if (a[0] == 'r')   // step with range
                    {
                        frame.flags.trap = true;
                        thread::detail::thread_details::set_trap(t);
                        step_range_begin = rbegin;
                        step_range_end = rend;
                        action = step_range;
                    }
                    else if (a[0] == 't')   // stop
                    {
                        t->suspend();
                        action = stop;
                    }
                    if (thread::detail::thread_details::trap_state(t) && t->id() != current_thread_id) use_sigcont = true;
                }

                bool do_action()
                {
                    switch (action)
                    {
                    case step_sig:
                    case cont_sig:
                        return false;
                    case step:
                    case cont:
                    case step_range:
                        return true;
                    default: throw std::exception();
                    }
                }
            };
            
            std::map<std::uint32_t, thread_info, std::less<std::uint32_t>, locked_pool_allocator<>> threads { alloc };
            thread_info* current_thread { nullptr };

            void populate_thread_list()
            {
                for (auto i = threads.begin(); i != threads.end();)
                {
                    if (!i->second.thread.lock()) i = threads.erase(i);
                    else ++i;
                }
                for (auto& t : jw::thread::detail::scheduler::get_threads())
                {
                    threads[t->id()].thread = t;
                }
                current_thread_id = jw::thread::detail::scheduler::get_current_thread_id();
                threads[current_thread_id].thread = jw::thread::detail::scheduler::get_current_thread();
                current_thread = &threads[current_thread_id];
            }

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
                case 0x01:
                case 0x03:
                    if (threads[selected_thread_id].use_sigcont)
                    {
                        threads[selected_thread_id].use_sigcont = false;
                        return 0x13;    // SIGCONT
                    }
                    else return 0x05;   // SIGTRAP
                case 0x00: return 0x08; // SIGFPE
                case 0x02: return 0x09; // SIGKILL
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

            // Decode big-endian hex string
            inline auto decode(const std::string& s)
            {
                return std::stoul(s, nullptr, 16);
            }

            // Decode little-endian hex string
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

            // Encode little-endian hex string
            template <typename T>
            void encode(std::ostream& out, T* in, std::size_t len = sizeof(T))
            {
                auto ptr = reinterpret_cast<const byte*>(in);
                for (std::size_t i = 0; i < len; ++i) 
                    out << std::setw(2) << static_cast<std::uint32_t>(ptr[i]);
            }

            // Encode big-endian hex string
            template <typename T>
            void reverse_encode(std::ostream& out, T* in, std::size_t len = sizeof(T))
            {
                auto ptr = reinterpret_cast<const byte*>(in);
                for (std::size_t i = 0; i < len; ++i)
                    out << std::setw(2) << static_cast<std::uint32_t>(ptr[len - i - 1]);
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
                case '-': if (sent.size() > 0) send_packet(sent.front());
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

                std::deque<packet_string> parsed_input { };
                std::size_t pos { 1 };
                if (input.size() == 1) parsed_input.emplace_back("", input[0]);
                while (pos < input.size())
                {
                    auto p = std::min({ input.find(',', pos), input.find(':', pos), input.find(';', pos), input.find('=', pos) });
                    if (p == input.npos) p = input.size();
                    parsed_input.emplace_back(input.substr(pos, p - pos), input[pos - 1]);
                    pos += p - pos + 1;
                }
                return parsed_input;
            }

            void thread_reg(std::ostream& out, regnum r)
            {
                auto t = threads[selected_thread_id].thread.lock();
                if (!t)
                {
                    encode_null(out, reglen[r]);
                    return;
                }
                auto* reg = thread::detail::thread_details::get_context(t);   
                auto r_esp = reinterpret_cast<const std::uintptr_t*>(reg);
                switch (r)
                {
                case ebx: encode(out, &reg->ebx); return;
                case ebp: encode(out, &reg->ebp); return;
                case esi: encode(out, &reg->esi); return;
                case edi: encode(out, &reg->edi); return;
                case esp: encode(out, &r_esp); return;
                case cs: encode(out, &current_thread->frame.fault_address.segment); return;
                case ss: encode(out, &current_thread->frame.stack.segment); return;
                case ds: encode(out, &current_thread->frame.stack.segment); return;
                case es: encode(out, &reg->es, reglen[r]); return; 
                case fs: encode(out, &reg->fs, reglen[r]); return;
                case gs: encode(out, &reg->gs, reglen[r]); return;
                case eip: encode(out, r_esp - 1); return;
                default: encode_null(out, reglen[r]);
                }
            }

            void reg(std::ostream& out, regnum r, cpu_registers* reg, exception_frame* frame, bool new_type)
            {
                if (selected_thread_id != current_thread_id)
                {
                    thread_reg(out, r);
                    return;
                }
                auto* new_frame = static_cast<new_exception_frame*>(frame);
                switch (r)
                {
                case eax: encode(out, &reg->eax); return;
                case ebx: encode(out, &reg->ebx); return;
                case ecx: encode(out, &reg->ecx); return;
                case edx: encode(out, &reg->edx); return;
                case ebp: 
                    if (reg->ebp >= frame->stack.offset) encode(out, &reg->ebp);
                    else encode_null(out, reglen[r]);
                    return;
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
                    //if (current_thread->last_exception == 0x01) eip = current_thread->last_eip;   // TODO: is this necessary?
                    //else if (current_thread->last_exception == 0x03) eip -= 1;
                    encode(out, &eip);
                    return;
                }
                default: 
                    encode_null(out, reglen[r]);
                    return;
                    if (r > mxcsr) return;
                    auto fpu = detail::fpu_context_switcher.get_last_context();
                    switch (r)
                    {
                    default:; // TODO
                    }
                }
            }

            bool setreg(regnum r, const std::string& value, cpu_registers* reg, exception_frame* frame, bool new_type)
            {
                if (selected_thread_id != current_thread_id) return false;
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

            void stop_reply(bool async = false)
            {
                if (!async && selected_thread_id != current_thread_id)
                {
                    send_packet("S00");
                    return;
                }
                auto* r = &current_thread->reg;
                auto* f = &current_thread->frame;
                bool t = false;
                auto exc = current_thread->last_exception;
                std::stringstream s { };
                s << std::hex << std::setfill('0');
                if (async) s << "Stop:";
                if (exc == 0x01 || exc == 0x03)
                {
                    s << "T" << std::setw(2) << signal_number(exc);
                    s << eflags << ':'; reg(s, eflags, r, f, t); s << ';';
                    s << eip << ':'; reg(s, eip, r, f, t); s << ';';
                    s << esp << ':'; reg(s, esp, r, f, t); s << ';';
                    s << "thread:" << current_thread_id << ';';
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
                    if (async) send_notification(s.str());
                    else send_packet(s.str());
                }
                else
                {
                    s << "S" << std::setw(2) << signal_number(exc);
                    if (async)
                    {
                        s << ';';
                        send_notification(s.str());
                    }
                    else send_packet(s.str());
                }
            }

            [[gnu::hot]] bool handle_packet(exception_num, cpu_registers* r, exception_frame* f, bool t)
            {
                using namespace std;
                std::deque<packet_string> packet { };
                while (true)
                {
                    std::stringstream s { };
                    s << hex << setfill('0');
                    if (current_thread->action != thread_info::none)
                    {
                        stop_reply();
                        current_thread->action = thread_info::none;
                    }
                    packet = recv_packet();
                    auto& p = packet.front().delim;
                    if (p == '?')   // stop reason
                    {
                        stop_reply();
                    }
                    else if (p == 'q')  // query
                    {
                        auto& q = packet[0];
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
                            send_packet("PacketSize=100000;swbreak+;hwbreak+");
                        }
                        else if (q == "Attached") send_packet("0");
                        else if (q == "C")
                        {
                            s << "QC" << current_thread_id;
                            send_packet(s.str());
                        }
                        else if (q == "fThreadInfo")
                        {
                            s << "m";
                            for (auto& t : threads) 
                            {
                                s << t.first << ',';
                            }
                            send_packet(s.str());
                        }
                        else if (q == "sThreadInfo") send_packet("l");
                        else if (q == "ThreadExtraInfo")
                        {
                            using namespace thread::detail;
                            std::stringstream msg { };
                            auto id = decode(packet[1]);
                            if (threads.count(id))
                            {
                                auto t = threads[id].thread.lock();
                                msg << t->name;
                                if (id == current_thread_id) msg << " (*)";
                                msg << ": ";
                                switch (t->get_state())
                                {
                                case initialized: msg << "Initialized"; break;
                                case starting:    msg << "Starting";    break;
                                case running:     msg << "Running";     break;
                                case suspended:   msg << "Suspended";   break;
                                case terminating: msg << "Terminating"; break;
                                case finished:    msg << "Finished";    break;
                                }
                                if (t->pending_exceptions()) msg << ", " << t->pending_exceptions() << " pending exception(s)!";
                            }
                            else msg << "invalid thread";
                            auto str = msg.str();
                            encode(s, str.c_str(), str.size());
                            send_packet(s.str());
                        }
                        else send_packet("");
                    }
                    else if (p == 'v')
                    {
                        auto& v = packet[0];
                        if (v == "Stopped")
                        {
                            stop_reply();
                        }
                        else if (v == "Cont?")
                        {
                            send_packet("vCont;s;S;c;C;t;r");
                        }
                        else if (v == "Cont")
                        {
                            for (std::size_t i = 1; i < packet.size(); ++i)
                            {
                                if (packet[i][0] == 'r')
                                {
                                    auto begin = decode(packet[i].substr(1));
                                    ++i;
                                    auto end = decode(packet[i]);
                                    if (packet.size() >= i && packet[i + 1].delim == ':')
                                    {
                                        auto id = decode(packet[i + 1]);
                                        threads[id].set_action(packet[i - 1], 0, begin, end);
                                        ++i;
                                    }
                                    else send_packet("E00");
                                }
                                else if (packet.size() >= i && packet[i + 1].delim == ':')
                                {
                                    auto id = decode(packet[i + 1]);
                                    threads[id].set_action(packet[i]);
                                    ++i;
                                }
                                else
                                {
                                    for (auto& t : threads)
                                    {
                                        if (t.second.action == thread_info::none)
                                            t.second.set_action(packet[i]);
                                    }
                                }
                            }
                        }
                        else send_packet("");
                    }
                    else if (p == 'H')  // set current thread
                    {
                        auto id = decode(packet[0].substr(1));
                        if (threads.count(id))
                        {
                            selected_thread_id = id;
                            send_packet("OK");
                        }
                        else send_packet("E00");
                    }
                    else if (p == 'T')  // is thread alive?
                    {
                        auto id = decode(packet[0]);
                        if (threads.count(id)) send_packet("OK");
                        else send_packet("E01");
                    }
                    else if (p == 'p')  // read one register
                    {
                        auto regn = static_cast<regnum>(decode(packet[0]));
                        reg(s, regn, r, f, t);
                        send_packet(s.str());
                    }
                    else if (p == 'P')  // write one register
                    {
                        if (setreg(static_cast<regnum>(decode(packet[0])), packet[1], r, f, t)) send_packet("OK");
                        else send_packet("E00");
                    }
                    else if (p == 'g')  // read registers
                    {
                        for (int i = eax; i <= eflags; ++i)
                            reg(s, static_cast<regnum>(i), r, f, t);
                        send_packet(s.str());
                    }
                    else if (p == 'G')  // write registers
                    {
                        regnum reg { };
                        std::size_t pos { };
                        bool fail { false };
                        while (pos < packet[0].size())
                        {
                            if (fail |= setreg(reg, packet[0].substr(pos), r, f, t))
                            {
                                send_packet("E00");
                                break;
                            }
                            pos += reglen[reg] * 2;
                            ++reg;
                        }
                        if (!fail) send_packet("OK");
                    }
                    else if (p == 'm')  // read memory
                    {
                        auto* addr = reinterpret_cast<byte*>(decode(packet[0]));
                        std::size_t len = decode(packet[1]);
                        encode(s, addr, len);
                        send_packet(s.str());
                    }
                    else if (p == 'M')  // write memory
                    {
                        auto* addr = reinterpret_cast<byte*>(decode(packet[0]));
                        std::size_t len = decode(packet[1]);
                        if (reverse_decode(packet[2], addr, len)) send_packet("OK");
                        else send_packet("E00");
                    }
                    else if (p == 'c' || p == 's')  // step/continue
                    {
                        auto& t = threads[selected_thread_id];
                        if (packet.size() > 0) t.frame.fault_address.offset = decode(packet[0]);
                        t.set_action(packet[0].delim + std::string { });
                    }
                    else if (p == 'C' || p == 'S')  // step/continue with signal
                    {
                        auto& t = threads[selected_thread_id];
                        if (packet.size() > 1) t.frame.fault_address.offset = decode(packet[1]);
                        t.set_action(packet[0].delim + packet[0]);
                    }
                    else if (p == 'Z')  // set break/watchpoint
                    {
                        auto& z = packet[0][0];
                        std::uintptr_t addr = decode(packet[1]);
                        auto ptr = reinterpret_cast<byte*>(addr);
                        if (z == '0')   // set breakpoint
                        {
                            if (packet.size() > 3)  // conditional breakpoint
                            {
                                send_packet("");    // not implemented (TODO)
                                continue;
                            }
                            if (*ptr != 0xcc)
                            {
                                breakpoints.emplace(addr, *ptr);
                                *ptr = 0xcc;
                            }
                            send_packet("OK");
                        }
                        else            // set watchpoint
                        {
                            watchpoint::watchpoint_type w;
                            if (z == '1') w = watchpoint::execute;
                            else if (z == '2') w = watchpoint::read_write;
                            else if (z == '3') w = watchpoint::read;
                            else if (z == '4') w = watchpoint::read_write;
                            else
                            {
                                send_packet("");
                                continue;
                            }
                            try
                            {
                                std::size_t size = decode(packet[2]);
                                watchpoints.emplace(addr, watchpoint { ptr, w, size });
                                send_packet("OK");
                            }
                            catch (...)
                            {
                                send_packet("E00");
                            }
                        }
                    }
                    else if (p == 'z')  // remove break/watchpoint
                    {
                        auto& z = packet[0][0];
                        std::uintptr_t addr = decode(packet[1]);
                        auto ptr = reinterpret_cast<byte*>(addr);
                        if (z == '0')   // remove breakpoint
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
                    else if (p == 'k')  // kill
                    {
                        f->flags.trap = false;
                        f->stack.offset -= 4;                                                               // "sub esp, 4"
                        f->stack.offset &= -0x10;                                                           // "and esp, -0x10"
                        *reinterpret_cast<std::uintptr_t*>(f->stack.offset) = f->fault_address.offset;      // "mov [esp], eip"
                        f->fault_address.offset = reinterpret_cast<std::uintptr_t>(jw::terminate);          // "mov eip, func"
                        f->info_bits.redirect_elsewhere = true;
                        return true;
                    }
                    else send_packet("");   // unknown packet
                    if (current_thread->action != thread_info::none) return current_thread->do_action();
                }
            }

            [[gnu::hot]] bool handle_exception(exception_num exc, cpu_registers* r, exception_frame* f, bool t)
            {
                if (debugmsg) std::clog << "entering exception 0x" << std::hex << exc << "\n";

                if (__builtin_expect(debugger_reentry, false))
                {
                    if (exc == 0x01)    // watchpoint trap, ignore
                    {
                        if (debugmsg) std::clog << "reentry caused by watchpoint, ignoring.\n";
                        return true;
                    }
                    if (exc == 0x03)    // breakpoint in debugger code, remove and ignore
                    {
                        if (debugmsg) std::clog << "reentry caused by breakpoint, ignoring.\n";
                        if (breakpoints.count(f->fault_address.offset - 1))
                        {
                            f->fault_address.offset -= 1;
                            *reinterpret_cast<byte*>(f->fault_address.offset) = breakpoints[f->fault_address.offset];
                            if (debugmsg) std::clog << "breakpoint removed at 0x" << std::hex << f->fault_address.offset << "\n";
                        }
                        return true;
                    }
                    if (current_thread->action == thread_info::none) send_packet("EEE"); // last command caused another exception
                    if (debugmsg) std::clog << *static_cast<new_exception_frame*>(f) << *r;
                    current_thread->frame.info_bits.redirect_elsewhere = true;
                    detail::fpu_context_switcher.leave();
                    --detail::exception_count;      // pretend it never happened.
                }
                else
                {
                    debugger_reentry = true;
                    populate_thread_list();
                    if (__builtin_expect(exc == 0x01 || exc == 0x03, true))
                    {
                        if (thread::detail::thread_details::trap_is_masked(current_thread->thread.lock()))
                        {
                            current_thread->use_sigcont = true;
                            thread::detail::thread_details::set_trap(current_thread->thread.lock());
                            f->flags.trap = false;
                            debugger_reentry = false;
                            current_thread->last_eip = f->fault_address.offset;
                            if (debugmsg) std::clog << "trap masked at 0x" << std::hex << f->fault_address.offset << ", resuming with SIGCONT.\n";
                            return true;
                        }
                        if (current_thread->action == thread_info::step_range &&
                            f->fault_address.offset >= current_thread->step_range_begin &&
                            f->fault_address.offset <= current_thread->step_range_end)
                        {
                            debugger_reentry = false;
                            current_thread->last_eip = f->fault_address.offset;
                            if (debugmsg) std::clog << "range step until 0x" << std::hex << current_thread->step_range_end;
                            if (debugmsg) std::clog << ", now at 0x" << f->fault_address.offset << '\n';
                            return true;
                        }
                        thread::detail::thread_details::clear_trap(current_thread->thread.lock());
                    }
                    if (debugmsg) std::clog << *static_cast<new_exception_frame*>(f) << *r;
                    if (t) current_thread->frame = *static_cast<new_exception_frame*>(f);
                    else static_cast<old_exception_frame&>(current_thread->frame) = *f;
                    current_thread->reg = *r;
                    current_thread->last_exception = exc; 
                    if (exc == 0x03) current_thread->frame.fault_address.offset -= 1;
                    selected_thread_id = current_thread_id;
                    //stop_reply(true);
                    if (current_thread->action == thread_info::none) current_thread->action = thread_info::cont;
                }

                if (config::enable_gdb_interrupts) asm("sti");
                bool result { false };
                try
                {
                    result = handle_packet(current_thread->last_exception, &current_thread->reg, &current_thread->frame, t);
                    for (auto& w : watchpoints) w.second.reset();
                }
                catch (...) 
                { 
                    std::cerr << "Exception occured while communicating with GDB.\n";
                    //asm("int 3");
                }
                asm("cli");

                if (t) *f = static_cast<new_exception_frame&>(current_thread->frame);
                else *static_cast<old_exception_frame*>(f) = current_thread->frame;
                *r = current_thread->reg;
                current_thread->last_eip = current_thread->frame.fault_address.offset;
                debugger_reentry = false;

                if (debugmsg) std::clog << "leaving exception 0x" << std::hex << exc << "\n";
                return result;
            }

            bool gdb_interface_setup { false };
            void setup_gdb_interface(std::unique_ptr<std::iostream, allocator_delete<jw::dpmi::locking_allocator<std::iostream>>>&& stream)
            {
                if (gdb_interface_setup) return;
                gdb_interface_setup = true;

                gdb = std::move(stream);

                exception_handlers[0x00] = std::make_unique<exception_handler>(0x00, [](auto* r, auto* f, bool t) { return handle_exception(0x00, r, f, t); });
                exception_handlers[0x01] = std::make_unique<exception_handler>(0x01, [](auto* r, auto* f, bool t) { return handle_exception(0x01, r, f, t); });
                exception_handlers[0x02] = std::make_unique<exception_handler>(0x02, [](auto* r, auto* f, bool t) { return handle_exception(0x02, r, f, t); });
                exception_handlers[0x03] = std::make_unique<exception_handler>(0x03, [](auto* r, auto* f, bool t) { return handle_exception(0x03, r, f, t); });
                exception_handlers[0x04] = std::make_unique<exception_handler>(0x04, [](auto* r, auto* f, bool t) { return handle_exception(0x04, r, f, t); });
                exception_handlers[0x05] = std::make_unique<exception_handler>(0x05, [](auto* r, auto* f, bool t) { return handle_exception(0x05, r, f, t); });
                exception_handlers[0x06] = std::make_unique<exception_handler>(0x06, [](auto* r, auto* f, bool t) { return handle_exception(0x06, r, f, t); });
                if (!detail::test_cr0_access())
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
                exception_handlers[0x10] = std::make_unique<exception_handler>(0x10, [](auto* r, auto* f, bool t) { return handle_exception(0x10, r, f, t); });
                exception_handlers[0x11] = std::make_unique<exception_handler>(0x11, [](auto* r, auto* f, bool t) { return handle_exception(0x11, r, f, t); });
                exception_handlers[0x12] = std::make_unique<exception_handler>(0x12, [](auto* r, auto* f, bool t) { return handle_exception(0x12, r, f, t); });
                exception_handlers[0x13] = std::make_unique<exception_handler>(0x13, [](auto* r, auto* f, bool t) { return handle_exception(0x13, r, f, t); });
                exception_handlers[0x14] = std::make_unique<exception_handler>(0x14, [](auto* r, auto* f, bool t) { return handle_exception(0x14, r, f, t); });
                exception_handlers[0x1e] = std::make_unique<exception_handler>(0x1e, [](auto* r, auto* f, bool t) { return handle_exception(0x1e, r, f, t); });
            }
        }

        bool debug() noexcept { return detail::gdb_interface_setup; }

        trap_mask::trap_mask() noexcept // TODO: ideally this should treat interrupts as separate 'threads'
        {
            if (!debug()) return;
            if (detail::debugger_reentry) return;
            auto t = jw::thread::detail::scheduler::get_current_thread().lock();
            if (t) thread::detail::thread_details::trap_mask(t);
            else fail = true;
        }

        trap_mask::~trap_mask() noexcept
        {
            if (fail) return;
            if (!debug()) return;
            if (detail::debugger_reentry) return;
            auto t = jw::thread::detail::scheduler::get_current_thread().lock();
            if (thread::detail::thread_details::trap_unmask(t) && thread::detail::thread_details::trap_state(t)) asm("int 3");
        }
    #endif
    }
}
