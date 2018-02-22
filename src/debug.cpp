/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <array>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <memory>
#include <csignal>
#include <cstdlib>
#include <string_view>
#include <unordered_set>
#include <jw/dpmi/fpu.h>
#include <jw/dpmi/dpmi.h>
#include <jw/dpmi/debug.h>
#include <jw/dpmi/cpu_exception.h>
#include <jw/io/rs232.h>
#include <jw/alloc.h>
#include <../jwdpmi_config.h>

// TODO: optimize

using namespace std::string_literals;

extern void* context_switch_end asm("context_switch_end");

namespace jw
{
    namespace dpmi
    {
#       ifndef NDEBUG
        namespace detail
        {
            constexpr bool is_benign_signal(std::int32_t) noexcept;
            constexpr bool all_benign_signals(auto*);

            enum signals
            {
                packet_received = 0x1010,
                trap_masked,
                trap_unmasked,
                continued
            };

            struct rs232_streambuf_internals : public io::detail::rs232_streambuf
            {
                using rs232_streambuf::rs232_streambuf;
                const char* get_gptr() const { return gptr(); }
                const char* get_egptr() const { return egptr(); }
            };

            const bool debugmsg = config::enable_gdb_debug_messages;

            volatile bool debugger_reentry { false };
            bool debug_mode { false };
            bool killed { false };
            volatile int current_signal { -1 };
            bool thread_events_enabled { false };
            bool new_frame_type { true };

            locked_pool_allocator<> alloc { 1_MB };
            std::map<std::string, std::string, std::less<std::string>, locked_pool_allocator<>> supported { alloc };
            std::map<std::uintptr_t, watchpoint, std::less<std::uintptr_t>, locked_pool_allocator<>> watchpoints { alloc };
            std::map<std::uintptr_t, byte, std::less<std::uintptr_t>, locked_pool_allocator<>> breakpoints { alloc };
            std::map<int, void(*)(int)> signal_handlers {  };

            std::array<std::unique_ptr<exception_handler>, 0x20> exception_handlers;
            rs232_streambuf_internals* gdb_streambuf;
            std::unique_ptr<std::iostream, allocator_delete<jw::dpmi::locking_allocator<std::iostream>>> gdb;
            std::unique_ptr<dpmi::irq_handler> serial_irq;

            struct packet_string : public std::string_view
            {
                char delim;
                template <typename T, typename U>
                packet_string(T&& str, U&& delimiter): std::string_view(std::forward<T>(str)), delim(std::forward<U>(delimiter)) { }
                using std::string_view::operator=;
                using std::string_view::basic_string_view;
            };

            std::basic_string<char, std::char_traits<char>, locked_pool_allocator<>> last_sent_packet { alloc };
            std::basic_string<char,std::char_traits<char>,locked_pool_allocator<>> raw_packet_string { alloc };
            std::deque<packet_string, locked_pool_allocator<>> packet { alloc };

            std::uint32_t current_thread_id { 1 };
            std::map<char, std::uint32_t, std::less<char>, locked_pool_allocator<>> selected_thread_id { alloc };
            constexpr auto all_threads_id { std::numeric_limits<std::uint32_t>::max() };

            struct thread_info
            {
                std::weak_ptr<thread::detail::thread> thread;
                new_exception_frame frame;
                cpu_registers reg;
                std::unordered_multiset<std::int32_t, std::hash<std::int32_t>, std::equal_to<std::int32_t>, locked_pool_allocator<>> signals { alloc };
                std::int32_t last_stop_signal { -1 };
                std::uintptr_t step_range_begin { 0 };
                std::uintptr_t step_range_end { 0 };

                void set_trap() noexcept { thread::detail::thread_details::set_trap(thread.lock()); }
                bool trap_is_masked() noexcept { return thread::detail::thread_details::trap_masked(thread.lock()) > 0; }
                void clear_trap() noexcept { thread::detail::thread_details::clear_trap(thread.lock()); }
                
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

                void set_action(char a, std::uintptr_t resume_at = 0, std::uintptr_t rbegin = 0, std::uintptr_t rend = 0)
                {
                    if (resume_at != 0) frame.fault_address.offset = resume_at;
                    auto t = thread.lock();
                    t->resume();
                    if (a == 'c')  // continue
                    {
                        frame.flags.trap = false;
                        clear_trap();
                        action = cont;
                    }
                    else if (a == 's')  // step
                    {
                        frame.flags.trap = signals.count(trap_masked) == 0;
                        set_trap();
                        action = step;
                    }
                    else if (a == 'C')  // continue with signal
                    {
                        frame.flags.trap = false;
                        clear_trap();
                        if (all_benign_signals(this)) action = cont;
                        else action = cont_sig;
                    }
                    else if (a == 'S')  // step with signal
                    {
                        frame.flags.trap = signals.count(trap_masked) == 0;
                        set_trap();
                        if (all_benign_signals(this)) action = step;
                        else action = step_sig;
                    }
                    else if (a == 'r')   // step with range
                    {
                        frame.flags.trap = signals.count(trap_masked) == 0;
                        set_trap();
                        step_range_begin = rbegin;
                        step_range_end = rend;
                        action = step_range;
                    }
                    else if (a == 't')   // stop
                    {
                        t->suspend();
                        clear_trap();
                        action = stop;
                    }
                }

                bool do_action() const
                {
                    switch (action)
                    {
                    case step_sig:
                    case cont_sig:
                        return false;
                    case step:
                    case cont:
                    case step_range:
                    case stop:
                        return true;
                    default: throw std::exception();
                    }
                }
            };
            
            std::map<std::uint32_t, thread_info, std::less<std::uint32_t>, locked_pool_allocator<>> threads { alloc };
            thread_info* current_thread { nullptr };

            void populate_thread_list()
            {
                for (auto&& t : jw::thread::detail::scheduler::get_threads())
                {
                    threads[t->id()].thread = t;
                }
                current_thread_id = jw::thread::detail::scheduler::get_current_thread_id();
                threads[current_thread_id].thread = jw::thread::detail::scheduler::get_current_thread();
                current_thread = &threads[current_thread_id];
            }

            void erase_expired_threads()
            {
                for (auto i = threads.begin(); i != threads.end();)
                {
                    if (i->second.thread.expired()) i = threads.erase(i);
                    else ++i;
                }
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

            constexpr std::array<std::size_t, 41> reglen
            {
                4, 4, 4, 4,
                4, 4, 4, 4,
                4, 4,
                2, 2, 2, 2, 2, 2,
                10, 10, 10, 10, 10, 10, 10, 10,
                4, 4, 4, 4, 4, 4, 4, 4,
                16, 16, 16, 16, 16, 16, 16, 16,
                4
            };

#           ifndef __SSE__
            constexpr auto reg_max = regnum::fop;
#           else
            constexpr auto reg_max = regnum::mxcsr;
#           endif

            enum posix_signals : std::int32_t
            {
                sighup = 1,
                sigint,
                sigquit,
                sigill,
                sigtrap,
                sigabrt,
                sigemt,
                sigfpe,
                sigkill,
                sigbus,
                sigsegv,
                sigsys,
                sigpipe,
                sigalrm,
                sigterm,
                sigstop = 17,
                sigcont = 19,
                sigusr1 = 30,
                sigusr2 = 31,
            };

            constexpr std::uint32_t posix_signal(std::int32_t exc) noexcept
            {
                switch (exc)
                {
                    // cpu exception -> posix signal
                case exception_num::trap:
                case exception_num::breakpoint:
                    return sigtrap;

                case exception_num::divide_error:
                case exception_num::overflow:
                case exception_num::x87_exception:
                case exception_num::sse_exception:
                    return sigfpe;

                case exception_num::non_maskable_interrupt:
                case exception_num::double_fault:
                    return sigkill;

                case exception_num::bound_range_exceeded:
                case exception_num::x87_segment_not_present:
                case exception_num::invalid_tss:
                case exception_num::segment_not_present:
                case exception_num::stack_segment_fault:
                case exception_num::general_protection_fault:
                case exception_num::page_fault:
                    return sigsegv;

                case exception_num::invalid_opcode:
                    return sigill;

                case exception_num::device_not_available:
                    return sigemt;

                case exception_num::alignment_check:
                case exception_num::machine_check:
                    return sigbus;

                    // djgpp signal -> posix signal
                case SIGHUP:  return sighup;
                case SIGINT:  return sigint;
                case SIGQUIT: return sigquit;
                case SIGILL:  return sigill;
                case SIGABRT: return sigabrt;
                case SIGKILL: return sigkill;
                case SIGTERM: return sigterm;

                    // other signals
                case continued:
                    return sigcont;

                case -1:
                case packet_received:
                    return 0;

                case thread::detail::all_threads_suspended:
                case thread::detail::thread_finished:
                case thread::detail::thread_switched:
                    return sigstop;

                default: return sigusr1;
                }
            }

            inline bool is_stop_signal(thread_info& t, std::int32_t exc)
            {
                switch (exc)
                {
                default:
                    return true;

                case thread::detail::thread_switched:
                    if (t.action == thread_info::stop) return true;
                    if (thread_events_enabled and t.thread.lock()->get_state() == thread::detail::starting) return true;
                    return false;

                case thread::detail::thread_finished:
                    if (thread_events_enabled) return true;
                    else return false;

                case packet_received:
                case -1:
                    return false;
                }
            }

            constexpr bool is_benign_signal(std::int32_t exc) noexcept
            {
                switch (exc)
                {
                default:
                    return false;

                case exception_num::trap:
                case exception_num::breakpoint:
                case thread::detail::thread_switched:
                case thread::detail::thread_finished:
                case thread::detail::all_threads_suspended:
                case trap_masked:
                case trap_unmasked:
                case packet_received:
                case continued:
                case -1:
                    return true;
                }
            }

            constexpr bool all_benign_signals(auto* t)
            {
                for (auto&& s : t->signals)
                    if (not is_benign_signal(s)) return false;
                return true;
            }

            inline bool set_breakpoint(std::uintptr_t at)
            {
                auto* ptr = reinterpret_cast<byte*>(at);
                if (*ptr == 0xcc) return false;
                breakpoints[at] = *ptr;
                *ptr = 0xcc;
                return true;
            }

            inline bool clear_breakpoint(std::uintptr_t at)
            {
                auto* ptr = reinterpret_cast<byte*>(at);
                if (breakpoints.count(at) > 0)
                {
                    *ptr = breakpoints[at];
                    breakpoints.erase(at);
                    return true;
                }
                else return false;
            }

            inline bool disable_breakpoint(std::uintptr_t at)
            {
                auto* ptr = reinterpret_cast<byte*>(at);
                if (breakpoints.count(at) > 0)
                {
                    *ptr = breakpoints[at];
                    return true;
                }
                else return false;
            }

            inline void enable_all_breakpoints()
            {
                for (auto&& bp : breakpoints)
                    *reinterpret_cast<byte*>(bp.first) = 0xcc;
            }

            // Decode big-endian hex string
            inline auto decode(std::string_view str)
            {
                std::uint32_t result { };
                if (str[0] == '-') return all_threads_id;
                for (auto&& c : str)
                {
                    result <<= 4;
                    if (c >= 'a' and c <= 'f') result |= 10 + c - 'a';
                    else if (c >= '0' and c <= '9') result |= c - '0';
                    else throw std::invalid_argument { "decode() failed: "s + str.data() };
                }
                return result;
            }

            // Decode little-endian hex string
            template <typename T>
            bool reverse_decode(const std::string_view& in, T* out, std::size_t len = sizeof(T))
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

            std::uint32_t checksum(const std::string_view& s)
            {
                std::uint8_t r { 0 };
                for (auto&& c : s) r += c;
                return r;
            }

            bool packet_available()
            {
                auto* p = gdb_streambuf->get_gptr();
                std::size_t size = gdb_streambuf->get_egptr() - p;
                std::string_view str { p, size };
                return str.find(0x03) != std::string_view::npos
                    or str.find('#', str.find('$')) != std::string_view::npos;
            }

            // not used
            void send_notification(const std::string& output)
            {
                if (config::enable_gdb_protocol_dump) std::clog << "note --> \"" << output << "\"\n";
                const auto sum = checksum(output);
                *gdb << '%' << output << '#' << std::setfill('0') << std::hex << std::setw(2) << sum << std::flush;
            }
            
            void send_packet(const std::string_view& output)
            {
                if (config::enable_gdb_protocol_dump) std::clog << "send --> \"" << output << "\"\n";

                static std::string rle_output { };
                rle_output.clear();
                rle_output.reserve(output.size());
                auto& s = rle_output;

                for (auto i = output.cbegin(); i < output.cend();)
                {
                    auto j = i;
                    while (*j == *i and j != output.cend()) ++j;
                    auto count = j - i;
                    if (count > 3)
                    {
                        count = std::min(count, 98);    // above 98, rle byte would be non-printable
                        if (count == 7 or count == 8) count = 6;    // rle byte can't be '#' or '$'
                        s += *i;
                        s += '*';
                        s += static_cast<char>(count + 28);
                    }
                    else
                    {
                        s.append(count, *i);
                    }
                    i += count;
                }

                const auto sum = checksum(rle_output);
                *gdb << '$' << rle_output << '#' << std::setfill('0') << std::hex << std::setw(2) << sum;
                if (not interrupt_mask::get())
                    *gdb << std::flush;
                last_sent_packet = output;
            }

            void recv_packet()
            {
                static std::string sum;

            retry:
                if (not interrupt_mask::get()) *gdb << std::flush;
                switch (gdb->get())
                {
                case '-':
                    std::cerr << "NACK --> " << last_sent_packet << '\n';
                    send_packet(last_sent_packet);
                    [[fallthrough]];
                case '+': // last_sent_packet.clear();
                default: goto retry;
                case 0x03: raw_packet_string = "vCtrlC"; goto parse;
                case '$': break;
                }

                raw_packet_string.clear();
                std::getline(*gdb, raw_packet_string, '#');
                sum.clear();
                sum += gdb->get();
                sum += gdb->get();
                if (decode(sum) == checksum(raw_packet_string)) *gdb << '+';
                else 
                {
                    std::cerr << "BAD CHECKSUM: " << raw_packet_string << ": " << sum << ", calculated: " << checksum(raw_packet_string) << '\n';
                    *gdb << '-';
                    goto retry; 
                }

            parse:
                if (config::enable_gdb_protocol_dump) std::clog << "recv <-- \"" << raw_packet_string << "\"\n";
                std::size_t pos { 1 };
                packet.clear();
                std::string_view input { raw_packet_string };
                if (input.size() == 1) packet.emplace_back("", input[0]);
                while (pos < input.size())
                {
                    auto p = std::min({ input.find(',', pos), input.find(':', pos), input.find(';', pos), input.find('=', pos) });
                    if (p == input.npos) p = input.size();
                    packet.emplace_back(input.substr(pos, p - pos), input[pos - 1]);
                    pos += p - pos + 1;
                }
            }

            void reg(std::ostream& out, regnum r, std::uint32_t id)
            {
                auto&& t = threads[id];
                if (&t == current_thread)
                {
                    switch (r)
                    {
                    case eax: encode(out, &t.reg.eax); return;
                    case ebx: encode(out, &t.reg.ebx); return;
                    case ecx: encode(out, &t.reg.ecx); return;
                    case edx: encode(out, &t.reg.edx); return;
                    case ebp: encode(out, &t.reg.ebp); return;
                    case esi: encode(out, &t.reg.esi); return;
                    case edi: encode(out, &t.reg.edi); return;
                    case esp: encode(out, &t.frame.stack.offset); return;
                    case eflags: encode(out, &t.frame.flags.raw_eflags); return;
                    case cs: encode(out, &t.frame.fault_address.segment); return;
                    case ss: encode(out, &t.frame.stack.segment); return;
                    case ds: if (new_frame_type) encode(out, &t.frame.ds); else encode_null(out, reglen[r]); return;
                    case es: if (new_frame_type) encode(out, &t.frame.es); else encode_null(out, reglen[r]); return;
                    case fs: if (new_frame_type) encode(out, &t.frame.fs); else encode_null(out, reglen[r]); return;
                    case gs: if (new_frame_type) encode(out, &t.frame.gs); else encode_null(out, reglen[r]); return;
                    case eip:
                    {
                        auto eip = t.frame.fault_address.offset;
                        if (*reinterpret_cast<byte*>(eip) == 0xcc and not breakpoints.count(eip)) eip += 1;
                        encode(out, &eip);
                        return;
                    }
                    default:
                        encode_null(out, reglen[r]);
                        return;
                        if (r > mxcsr) return;
                        //auto fpu = detail::fpu_context_switcher.get_last_context();
                        switch (r)
                        {
                        default:; // TODO
                        }
                    }
                }
                else
                {
                    auto t_ptr = t.thread.lock();
                    if (not t_ptr or t_ptr->get_state() == thread::detail::starting)
                    {
                        encode_null(out, reglen[r]);
                        return;
                    }
                    auto* reg = thread::detail::thread_details::get_context(t_ptr);
                    auto r_esp = reinterpret_cast<const std::uintptr_t>(reg) - sizeof(thread::detail::thread_context);
                    auto* r_eip = &context_switch_end;
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
                    case eip: encode(out, &r_eip); return;
                    default: encode_null(out, reglen[r]);
                    }
                }
            }

            bool setreg(regnum r, const std::string_view& value, std::uint32_t id)
            {
                auto&& t = threads[id];
                if (&t != current_thread) return false; // TODO
                if (debugmsg) std::clog << "set register " << std::hex << r << '=' << value << '\n';
                switch (r)
                {
                case eax:    return reverse_decode(value, &t.reg.eax, reglen[r]);
                case ebx:    return reverse_decode(value, &t.reg.ebx, reglen[r]);
                case ecx:    return reverse_decode(value, &t.reg.ecx, reglen[r]);
                case edx:    return reverse_decode(value, &t.reg.edx, reglen[r]);
                case ebp:    return reverse_decode(value, &t.reg.ebp, reglen[r]);
                case esi:    return reverse_decode(value, &t.reg.esi, reglen[r]);
                case edi:    return reverse_decode(value, &t.reg.edi, reglen[r]);
                case esp:    return reverse_decode(value, &t.frame.stack.offset, reglen[r]);
                case eip:    return reverse_decode(value, &t.frame.fault_address.offset, reglen[r]);
                case eflags: return reverse_decode(value, &t.frame.flags.raw_eflags, reglen[r]);
                case cs:     return reverse_decode(value, &t.frame.fault_address.segment, reglen[r]);
                case ss:     return reverse_decode(value, &t.frame.stack.segment, reglen[r]);
                case ds: if (new_frame_type) { return reverse_decode(value, &t.frame.ds, reglen[r]); } return false;
                case es: if (new_frame_type) { return reverse_decode(value, &t.frame.es, reglen[r]); } return false;
                case fs: if (new_frame_type) { return reverse_decode(value, &t.frame.fs, reglen[r]); } return false;
                case gs: if (new_frame_type) { return reverse_decode(value, &t.frame.gs, reglen[r]); } return false;
                default: if (r > mxcsr) return false;
                    //auto fpu = detail::fpu_context_switcher.get_last_context();
                    switch (r)
                    {
                    default: return false; // TODO
                    }
                }
            }

            void stop_reply(bool force = false, bool async = false)
            {
                for (auto&& t : threads)
                {
                    auto no_stop_signals = [&t]
                    {
                        if (t.second.signals.empty()) return true;
                        for (auto&& i : t.second.signals) if (is_stop_signal(t.second, i)) return false;
                        return true;
                    };
                    if (no_stop_signals()) t.second.signals.insert(t.second.last_stop_signal);

                    for (auto i = t.second.signals.begin(); i != t.second.signals.end();)
                    {
                        auto signal = *i;
                        if (not is_stop_signal(t.second, signal) or (t.second.action == thread_info::none and not force))
                        {
                            ++i;
                            continue;
                        }
                        else i = t.second.signals.erase(i);

                        t.second.last_stop_signal = signal;

                        std::stringstream s { };
                        s << std::hex << std::setfill('0');
                        if (async) s << "Stop:";
                        if (signal == thread::detail::thread_finished)
                        {
                            if (not thread_events_enabled) continue;
                            s << 'w';
                            if (t.second.thread.lock()->get_state() == thread::detail::finished) s << "00";
                            else s << "ff";
                            s << ';' << t.first;
                            send_packet(s.str());
                        }
                        else
                        {
                            s << "T" << std::setw(2) << posix_signal(signal);
                            //s << eip << ':'; reg(s, eip, t.first); s << ';';   // TODO fix thread registers
                            //s << esp << ':'; reg(s, esp, t.first); s << ';';
                            //s << ebp << ':'; reg(s, ebp, t.first); s << ';';
                            s << "thread:" << t.first << ';';
                            if (t.second.thread.lock()->get_state() == thread::detail::starting)
                            {
                                if (thread_events_enabled) s << "create:;";
                            }
                            else if (posix_signal(signal) == sigtrap)
                            {
                                for (auto&& w : watchpoints)
                                {
                                    if (w.second.get_state())
                                    {
                                        if (w.second.get_type() == watchpoint::execute) s << "hwbreak:;";
                                        else s << "watch:" << w.first << ";";
                                        break;
                                    }
                                    else s << "swbreak:;";
                                }
                            }
                            if (async) send_notification(s.str());
                            else send_packet(s.str());
                        }
                        t.second.action = thread_info::none;

                        if (signal == thread::detail::all_threads_suspended and supported["no-resumed"] == "+")
                            send_packet("N");
                    }
                }
            }

            [[gnu::hot]] bool handle_packet()
            {
                auto cant_continue = []
                {
                    if (current_thread->signals.count(packet_received) > 0) return true;
                    for (auto&& t : threads)
                        if (t.second.thread.lock()->is_running() and
                            t.second.action == thread_info::none) return true;
                    return false;
                };

                while (cant_continue())
                {
                    recv_packet();
                    current_thread->signals.erase(packet_received);

                    std::stringstream s { };
                    s << std::hex << std::setfill('0');
                    auto& p = packet.front().delim;
                    selected_thread_id.try_emplace(p, current_thread_id);
                    if (p == '?')   // stop reason
                    {
                        stop_reply(true);
                    }
                    else if (p == 'q')  // query
                    {
                        auto& q = packet[0];
                        if (q == "Supported")
                        {
                            packet.pop_front();
                            for (auto&& str : packet)
                            {
                                auto back = str.back();
                                auto equals_sign = str.find('=', 0);
                                if (back == '+' or back == '-')
                                {
                                    supported[str.substr(0, str.size() - 1).data()] = back;
                                }
                                else if (equals_sign != str.npos)
                                {
                                    supported[str.substr(0, equals_sign).data()] = str.substr(equals_sign + 1);
                                }
                            }
                            send_packet("PacketSize=399;swbreak+;hwbreak+;QThreadEvents+;no-resumed+");
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
                            for (auto&& t : threads) 
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
                                auto&& t = threads[id].thread.lock();
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
                    else if (p == 'Q')
                    {
                        auto& q = packet[0];
                        if (q == "ThreadEvents")
                        {
                            thread_events_enabled = packet[1][0] - '0';
                            send_packet("OK");
                        }
                        else send_packet("");
                    }
                    else if (p == 'v')
                    {
                        auto& v = packet[0];
                        if (v == "Stopped")
                        {
                            stop_reply(true);
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
                                    if (packet.size() >= i and packet[i + 1].delim == ':')
                                    {
                                        auto id = decode(packet[i + 1]);
                                        threads[id].set_action(packet[i - 1][0], 0, begin, end);
                                        ++i;
                                    }
                                    else send_packet("E00");
                                }
                                else if (packet.size() >= i and packet[i + 1].delim == ':')
                                {
                                    auto id = decode(packet[i + 1]);
                                    threads[id].set_action(packet[i][0]);
                                    ++i;
                                }
                                else
                                {
                                    for (auto&& t : threads)
                                    {
                                        if (t.second.action == thread_info::none)
                                            t.second.set_action(packet[i][0]);
                                    }
                                }
                            }
                        }
                        else if (v == "CtrlC")
                        {
                            auto already_stopped = []
                            {
                                for (auto&& t : threads) if (t.second.action != thread_info::stop) return false;
                                return true;
                            };
                            send_packet("OK");
                            if (already_stopped()) stop_reply();
                            else
                            {
                                for (auto&& t : threads) t.second.set_action('t');
                                if (interrupt_count == 0 and exception_count == 1)
                                    stop_reply();    // breaking in interrupt context yields a useless stack trace
                            }
                        }
                        else send_packet("");
                    }
                    else if (p == 'H')  // set current thread
                    {
                        auto id = decode(packet[0].substr(1));
                        if (threads.count(id) > 0 or id == all_threads_id)
                        {
                            selected_thread_id[packet[0][0]] = id;
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
                        reg(s, regn, selected_thread_id[p]);
                        send_packet(s.str());
                    }
                    else if (p == 'P')  // write one register
                    {
                        if (setreg(static_cast<regnum>(decode(packet[0])), packet[1], selected_thread_id[p])) send_packet("OK");
                        else send_packet("E00");
                    }
                    else if (p == 'g')  // read registers
                    {
                        for (auto i = eax; i <= reg_max; ++i)
                            reg(s, i, selected_thread_id[p]);
                        send_packet(s.str());
                    }
                    else if (p == 'G')  // write registers
                    {
                        regnum reg { };
                        std::size_t pos { };
                        bool fail { false };
                        while (pos < packet[0].size())
                        {
                            if (fail |= setreg(reg, packet[0].substr(pos), selected_thread_id[p]))
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
                    else if (p == 'c' or p == 's')  // step/continue
                    {
                        auto id = selected_thread_id[p];
                        auto step_continue = [] (auto& t)
                        {
                            if (packet.size() > 0)
                            {
                                std::uintptr_t jmp = decode(packet[0]);
                                if (debugmsg and t.frame.fault_address.offset != jmp) std::clog << "JUMP to 0x" << std::hex << jmp << '\n';
                                t.frame.fault_address.offset = jmp;
                            }
                            t.set_action(packet[0].delim);
                        };
                        if (id == all_threads_id)
                            for (auto&& t : threads) step_continue(t.second);
                        else step_continue(threads[id]);
                        
                    }
                    else if (p == 'C' or p == 'S')  // step/continue with signal
                    {
                        auto id = selected_thread_id[p];
                        auto step_continue = [](auto& t)
                        {
                            if (packet.size() > 1)
                            {
                                std::uintptr_t jmp = decode(packet[1]);
                                if (debugmsg and t.frame.fault_address.offset != jmp) std::clog << "JUMP to 0x" << std::hex << jmp << '\n';
                                t.frame.fault_address.offset = jmp;
                            }
                            t.set_action(packet[0].delim);
                        };
                        if (id == all_threads_id)
                            for (auto&& t : threads) step_continue(t.second);
                        else step_continue(threads[id]);
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
                            if (set_breakpoint(addr)) send_packet("OK");
                            else send_packet("");
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
                        if (z == '0')   // remove breakpoint
                        {
                            if (clear_breakpoint(addr)) send_packet("OK");
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
                        killed = true;
                        for (auto&&t : threads) t.second.frame.flags.trap = false;
                        simulate_call(&current_thread->frame, jw::terminate);
                        s << "X" << std::setw(2) << posix_signal(*current_thread->signals.cbegin());
                        send_packet(s.str());
                        return true;
                    }
                    else send_packet("");   // unknown packet
                }
                return current_thread->do_action();
            }

            [[gnu::hot]] bool handle_exception(exception_num exc, cpu_registers* r, exception_frame* f, bool)
            {
                if (debugmsg) std::clog << "entering exception 0x" << std::hex << exc << " from 0x" << f->fault_address.offset << '\n';
                if (killed) return false;

                bool result { false };
                auto catch_exception = []
                {
                    std::cerr << "Exception occured while communicating with GDB.\n";
                    std::cerr << "caused by this packet: " << raw_packet_string << '\n';
                    do { } while (true);
                };

                if (exc == 0x03) f->fault_address.offset -= 1;

                try
                {
                    if (__builtin_expect(debugger_reentry, false) and (exc == 0x01 or exc == 0x03))
                    {   // breakpoint in debugger code, ignore
                        if (debugmsg) std::clog << "reentry caused by breakpoint, ignoring.\n";
                        if (exc == 0x01) for (auto&& w : watchpoints) if (w.second.get_type() == watchpoint::execute) w.second.reset();
                        result = true;
                        goto leave;
                    }
                    else if (__builtin_expect(debugger_reentry, false) and current_thread->action == thread_info::none)
                    {   // TODO: determine action based on last packet / signal
                        send_packet("E04"); // last command caused another exception
                        if (debugmsg) std::clog << "debugger re-entry!\n";
                        if (debugmsg) std::clog << *static_cast<new_exception_frame*>(f) << *r;
                        current_thread->frame.info_bits.redirect_elsewhere = true;
                        leave_exception_context();  // pretend it never happened.
                    }
                    else
                    {
                        debugger_reentry = true;
                        populate_thread_list();

                        if (exc == 0x03 and current_signal != -1)
                        {
                            if (debugmsg) std::clog << "break with signal 0x" << std::hex << current_signal << '\n';
                            current_thread->signals.insert(current_signal);
                            current_signal = -1;
                        }
                        else current_thread->signals.insert(exc);

                        if (current_thread->signals.count(trap_unmasked) > 0)
                        {
                            bool use_sigcont { false };
                            for (auto i = current_thread->signals.begin(); i != current_thread->signals.end();)
                            {
                                if (posix_signal(*i) == sigtrap)
                                {
                                    i = current_thread->signals.erase(i);
                                    use_sigcont = true;
                                }
                                else ++i;
                            }
                            if (use_sigcont)
                                current_thread->signals.insert(continued); // resume with SIGCONT so gdb won't get confused
                            current_thread->signals.erase(trap_masked);
                        }

                        if (packet_available()) current_thread->signals.insert(packet_received);

                        if (current_thread->trap_is_masked() and
                            all_benign_signals(current_thread) and
                            not current_thread->signals.count(thread::detail::thread_switched) and
                            not current_thread->signals.count(thread::detail::all_threads_suspended) and
                            not current_thread->signals.count(packet_received))
                        {
                            current_thread->set_trap(); // re-enable trap when last trap_mask destructs
                            f->flags.trap = false;      // disable trap for now
                            current_thread->signals.insert(trap_masked);
                            if (debugmsg) std::clog << "trap masked at 0x" << std::hex << f->fault_address.offset << ", resuming with SIGCONT.\n";
                            result = true;
                            goto leave;
                        }
                        else if (current_thread->trap_is_masked() and not all_benign_signals(current_thread))
                        {
                            auto&& t = current_thread->thread.lock();
                            current_thread->signals.erase(trap_masked);
                            while (thread::detail::thread_details::trap_masked(t))
                                thread::detail::thread_details::trap_unmask(t);     // serious fault occured, undo trap masks
                        }
                        else if ([] { for (auto&& s : current_thread->signals) if (posix_signal(s) != sigtrap) return false; return true; }() and
                            current_thread->action == thread_info::step_range and
                            f->fault_address.offset >= current_thread->step_range_begin and
                            f->fault_address.offset <= current_thread->step_range_end)
                        {
                            if (debugmsg) std::clog << "range step until 0x" << std::hex << current_thread->step_range_end;
                            if (debugmsg) std::clog << ", now at 0x" << f->fault_address.offset << '\n';
                            result = true;
                            goto leave;
                        }

                        if (debugmsg) std::clog << *static_cast<new_exception_frame*>(f) << *r;
                        if (new_frame_type) current_thread->frame = *static_cast<new_exception_frame*>(f);
                        else static_cast<old_exception_frame&>(current_thread->frame) = *f;
                        current_thread->reg = *r;

                        erase_expired_threads();

                        for (auto&&t : threads)
                        {
                            if (t.second.action == thread_info::none)
                            {
                                if (thread_events_enabled) t.second.set_action('t');
                                else t.second.set_action('c');

                                if (t.second.thread.lock()->get_state() == thread::detail::starting)
                                    t.second.signals.insert(thread::detail::thread_switched);
                            }
                        }
                    }

                    if (config::enable_gdb_interrupts) asm("sti");
                    if (debugmsg)
                    {
                        std::clog << "signals:";
                        for (auto&& s : current_thread->signals) std::clog << " 0x" << std::hex << s;
                        std::clog << '\n';
                    }
                    stop_reply();
                    result = handle_packet();
                    for (auto&& w : watchpoints) w.second.reset();
                }
                catch (const std::exception& e) { print_exception(e); catch_exception(); }
                catch (...) { catch_exception(); }
                asm("cli");

                if (new_frame_type) *f = static_cast<new_exception_frame&>(current_thread->frame);
                else *static_cast<old_exception_frame*>(f) = current_thread->frame;
                *r = current_thread->reg;

            leave:
                enable_all_breakpoints();
                if (*reinterpret_cast<byte*>(f->fault_address.offset) == 0xcc
                    and not disable_breakpoint(f->fault_address.offset))
                    f->fault_address.offset += 1;    // don't resume on a breakpoint

                if (debugmsg) std::clog << "leaving exception 0x" << std::hex << exc << ", resuming at 0x" << f->fault_address.offset << '\n';

                debugger_reentry = false;
                return result;
            }

            void notify_gdb_thread_event(thread::detail::thread_event e)
            {
                if (thread_events_enabled) break_with_signal(e);
            }

            extern "C" void csignal(int signal)
            {
                if (not killed) break_with_signal(signal);
                signal_handlers[signal](signal);
            }

            void setup_gdb_interface(io::rs232_config cfg)
            {
                if (debug_mode) return;
                debug_mode = true;

                dpmi::locking_allocator<> stream_alloc;
                gdb_streambuf = new rs232_streambuf_internals { cfg };
                gdb = allocate_unique<io::rs232_stream>(stream_alloc, gdb_streambuf);

                serial_irq = std::make_unique<irq_handler>([]
                {
                    if (packet_available()) break_with_signal(packet_received);
                }, dpmi::always_call | dpmi::no_interrupts);

                {
                    exception_handler check_frame_type { 3, [](auto*, auto*, bool t) [[gnu::used]] 
                    { 
                        new_frame_type = t;
                        return true; 
                    } };
                    asm("int 3;");
                }

                for (auto&& s : { SIGHUP, SIGABRT, SIGTERM, SIGKILL, SIGQUIT, SIGILL, SIGINT })
                    signal_handlers[s] = std::signal(s, csignal);

                auto install_exception_handler = [](auto&& e) { exception_handlers[e] = std::make_unique<exception_handler>(e, [e](auto* r, auto* f, bool t) { return handle_exception(e, r, f, t); }); };

                for (auto&& e : { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e })
                    install_exception_handler(e);
                if (!detail::test_cr0_access())
                    install_exception_handler(0x07);

                serial_irq->set_irq(cfg.irq);
                serial_irq->enable();

                capabilities c { };
                if (!c.supported) return;
                if (std::strncmp(c.vendor_info.name, "HDPMI", 5) != 0) return;  // TODO: figure out if other hosts support these too
                for (auto&& e : { 0x10, 0x11, 0x12, 0x13, 0x14, 0x1e })
                    install_exception_handler(e);
            }

            void notify_gdb_exit(byte result)
            {
                killed = true;
                std::stringstream s { };
                s << std::hex << std::setfill('0');
                s << "W" << std::setw(2) << static_cast<std::uint32_t>(result);
                send_packet(s.str());
            }
        }

        trap_mask::trap_mask() noexcept // TODO: ideally this should treat interrupts as separate 'threads'
        {
            if (not debug()) { failed = true; return; }
            if (detail::debugger_reentry) { failed = true; return; }
            auto t = jw::thread::detail::scheduler::get_current_thread().lock();
            if (t) thread::detail::thread_details::trap_mask(t);
            else failed = true;
        }

        trap_mask::~trap_mask() noexcept
        {
            if (failed) return;
            auto t = jw::thread::detail::scheduler::get_current_thread().lock();
            if (thread::detail::thread_details::trap_masked(t) and
                thread::detail::thread_details::trap_unmask(t) and
                thread::detail::thread_details::trap_state(t))
                break_with_signal(detail::trap_unmasked);
        }
#       else
        void notify_gdb_thread_event(thread::detail::thread_event) { }
#       endif
    }
}
