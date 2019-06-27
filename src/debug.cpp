/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
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
#include <set>
#include <unwind.h>
#include <jw/dpmi/fpu.h>
#include <jw/dpmi/dpmi.h>
#include <jw/debug/debug.h>
#include <jw/dpmi/cpu_exception.h>
#include <jw/dpmi/detail/cpu_exception.h>
#include <jw/debug/detail/signals.h>
#include <jw/io/rs232.h>
#include <jw/alloc.h>
#include <jw/dpmi/ring0.h>
#include <../jwdpmi_config.h>

// TODO: optimize

using namespace std::string_literals;
using namespace jw::dpmi;
using namespace jw::dpmi::detail;

namespace jw
{
    namespace debug
    {
#       ifndef NDEBUG
        namespace detail
        {
            constexpr bool is_fault_signal(std::int32_t) noexcept;
            void uninstall_gdb_interface();

            struct rs232_streambuf_internals : public io::detail::rs232_streambuf
            {
                using rs232_streambuf::rs232_streambuf;
                const char* get_gptr() const { return gptr(); }
                const char* get_egptr() const { return egptr(); }
            };

            const bool debugmsg = config::enable_gdb_debug_messages;
            const bool temp_debugmsg = config::enable_gdb_debug_messages and true;

            volatile bool debugger_reentry { false };
            bool debug_mode { false };
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

            std::deque<std::string, locked_pool_allocator<>> sent_packets { alloc };
            std::string raw_packet_string;
            std::deque<packet_string, locked_pool_allocator<>> packet { alloc };
            bool replied { false };

            constexpr std::uint32_t all_threads_id { std::numeric_limits<std::uint32_t>::max() };
            std::uint32_t current_thread_id { 1 };
            std::uint32_t query_thread_id { 1 };
            std::uint32_t control_thread_id { all_threads_id };

            struct thread_info
            {
                std::weak_ptr<thread::detail::thread> thread;
                new_exception_frame frame;
                cpu_registers reg;
                //template <typename T> using set_with_alloc = std::unordered_set<T, std::hash<T>, std::equal_to<T>, locked_pool_allocator<T>>;
                template <typename T> using set_with_alloc = std::set<T, std::less<T>, locked_pool_allocator<T>>;
                set_with_alloc<std::int32_t> signals { alloc };
                std::int32_t last_stop_signal { -1 };
                std::uintptr_t step_range_begin { 0 };
                std::uintptr_t step_range_end { 0 };
                std::int32_t trap_mask { 0 };
                
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
                        action = cont;
                    }
                    else if (a == 's')  // step
                    {
                        frame.flags.trap = trap_mask == 0;
                        action = step;
                    }
                    else if (a == 'C')  // continue with signal
                    {
                        frame.flags.trap = false;
                        if (not is_fault_signal(last_stop_signal)) action = cont;
                        else action = cont_sig;
                    }
                    else if (a == 'S')  // step with signal
                    {
                        frame.flags.trap = trap_mask == 0;
                        if (not is_fault_signal(last_stop_signal)) action = step;
                        else action = step_sig;
                    }
                    else if (a == 'r')   // step with range
                    {
                        frame.flags.trap = trap_mask == 0;
                        step_range_begin = rbegin;
                        step_range_end = rend;
                        action = step_range;
                    }
                    else if (a == 't')   // stop
                    {
                        t->suspend();
                        action = stop;
                    }
                    else throw std::exception { };
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

            inline void populate_thread_list()
            {
                for (auto i = threads.begin(); i != threads.end();)
                {
                    if (i->second.thread.expired()) i = threads.erase(i);
                    else ++i;
                }
                for (auto&& t : jw::thread::detail::scheduler::get_threads())
                {
                    threads[t->id()].thread = t;
                }
                current_thread_id = jw::thread::detail::scheduler::get_current_thread_id();
                threads[current_thread_id].thread = jw::thread::detail::scheduler::get_current_thread();
                current_thread = &threads[current_thread_id];
            }

            // Register order and sizes found in {gdb source}/gdb/regformats/i386/i386.dat
            enum regnum
            {
                eax, ecx, edx, ebx,
                esp, ebp, esi, edi,
                eip, eflags,
                cs, ss, ds, es, fs, gs,
                st0, st1, st2, st3, st4, st5, st6, st7,
                fctrl, fstat, ftag, fiseg, fioff, foseg, fooff, fop,
                xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7,
                mxcsr
            };
            regnum& operator++(regnum& r) { return r = static_cast<regnum>(r + 1); }

            constexpr std::array<std::size_t, 41> regsize
            {
                4, 4, 4, 4,
                4, 4, 4, 4,
                4, 4,
                4, 4, 4, 4, 4, 4,
                10, 10, 10, 10, 10, 10, 10, 10,
                4, 4, 4, 4, 4, 4, 4, 4,
                16, 16, 16, 16, 16, 16, 16, 16,
                4
            };

#           ifndef HAVE__SSE__
            constexpr auto reg_max = regnum::fop;
#           else
            constexpr auto reg_max = regnum::mxcsr;
#           endif

            inline constexpr std::uint32_t posix_signal(std::int32_t exc) noexcept
            {
                switch (exc)
                {
                    // cpu exception -> posix signal
                case exception_num::trap:
                case exception_num::breakpoint:
                case watchpoint_hit:
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

                case all_threads_suspended:
                case thread_finished:
                case thread_suspended:
                case thread_started:
                    return sigstop;

                default: return sigusr1;
                }
            }

            inline constexpr bool is_fault_signal(std::int32_t exc) noexcept
            {
                switch (exc)
                {
                default:
                    return false;

                case exception_num::divide_error:
                case exception_num::overflow:
                case exception_num::x87_exception:
                case exception_num::sse_exception:
                case exception_num::non_maskable_interrupt:
                case exception_num::double_fault:
                case exception_num::bound_range_exceeded:
                case exception_num::x87_segment_not_present:
                case exception_num::invalid_tss:
                case exception_num::segment_not_present:
                case exception_num::stack_segment_fault:
                case exception_num::general_protection_fault:
                case exception_num::page_fault:
                case exception_num::invalid_opcode:
                case exception_num::device_not_available:
                case exception_num::alignment_check:
                case exception_num::machine_check:
                    return true;
                }
            }

            inline constexpr bool is_stop_signal(std::int32_t exc) noexcept
            {
                switch (exc)
                {
                default:
                    return true;

                case thread_switched:
                case packet_received:
                case trap_unmasked:
                case -1:
                    return false;
                }
            }

            inline constexpr bool is_trap_signal(std::int32_t exc) noexcept
            {
                switch (exc)
                {
                default:
                    return false;

                case exception_num::trap:
                case exception_num::breakpoint:
                case continued:
                case watchpoint_hit:
                    return true;
                }
            }

            inline constexpr bool is_benign_signal(std::int32_t exc) noexcept
            {
                switch (exc)
                {
                default:
                    return false;

                case exception_num::trap:
                case exception_num::breakpoint:
                case watchpoint_hit:
                case thread_switched:
                case thread_finished:
                case all_threads_suspended:
                case trap_unmasked:
                case packet_received:
                case continued:
                case -1:
                    return true;
                }
            }

            inline bool all_benign_signals(auto* t)
            {
                for (auto&& s : t->signals)
                    if (not is_benign_signal(s)) return false;
                return true;
            }

            inline void set_breakpoint(std::uintptr_t at)
            {
                auto* ptr = reinterpret_cast<byte*>(at);
                if (breakpoints.count(at) == 0) breakpoints[at] = *ptr;
                *ptr = 0xcc;
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
            inline bool reverse_decode(const std::string_view& in, T* out, std::size_t len = sizeof(T))
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
            inline void encode(std::ostream& out, T* in, std::size_t len = sizeof(T))
            {
                auto ptr = reinterpret_cast<const volatile byte*>(in);
                for (std::size_t i = 0; i < len; ++i) 
                    out << std::setw(2) << static_cast<std::uint32_t>(ptr[i]);
            }

            // Encode big-endian hex string
            template <typename T>
            inline void reverse_encode(std::ostream& out, T* in, std::size_t len = sizeof(T))
            {
                auto ptr = reinterpret_cast<const volatile byte*>(in);
                for (std::size_t i = 0; i < len; ++i)
                    out << std::setw(2) << static_cast<std::uint32_t>(ptr[len - i - 1]);
            }

            inline void encode_null(std::ostream& out, std::size_t len)
            {
                for (std::size_t i = 0; i < len; ++i) 
                    out << "xx";
            }

            inline std::uint32_t checksum(const std::string_view& s)
            {
                std::uint8_t r { 0 };
                for (auto&& c : s) r += c;
                return r;
            }

            inline bool packet_available()
            {
                auto* p = gdb_streambuf->get_gptr();
                std::size_t size = gdb_streambuf->get_egptr() - p;
                std::string_view str { p, size };
                bool result = str.find(0x03) != std::string_view::npos
                    or str.find('#', str.find('$')) != std::string_view::npos;
                //if (result) std::clog << "(packet available)";
                return result;
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
                *gdb << '$' << rle_output << '#' << std::setfill('0') << std::hex << std::setw(2) << sum << std::flush;
                sent_packets.emplace_back(output);
                replied = true;
            }

            void recv_ack()
            {
                *gdb << std::flush;
                if (gdb->rdbuf()->in_avail()) switch (gdb->peek())
                {
                case '-':
                    std::cerr << "NACK --> " << sent_packets.back() << '\n';
                    if (sent_packets.size() > 0) send_packet(sent_packets.back());
                    [[fallthrough]];
                case '+':
                    if (sent_packets.size() > 0) sent_packets.pop_back();
                    gdb->get();
                }
            }

            void recv_packet()
            {
                static std::string sum;

            retry:
                recv_ack();
                switch (gdb->peek())
                {
                default: gdb->get();
                case '+':
                case '-':
                    goto retry;
                case 0x03:
                    gdb->get();
                    raw_packet_string = "vCtrlC";
                    goto parse;
                case '$':
                    gdb->get();
                    break;
                }

                replied = false;
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
                if (threads.count(id) == 0)
                {
                    encode_null(out, regsize[r]);
                    return;
                }
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
                    case cs: { std::uint32_t s = t.frame.fault_address.segment; encode(out, &s); return; }
                    case ss: { std::uint32_t s = t.frame.stack.segment; encode(out, &s); return; }
                    case ds: { if (new_frame_type) { std::uint32_t s = t.frame.ds; encode(out, &s); } else encode_null(out, regsize[r]); return; }
                    case es: { if (new_frame_type) { std::uint32_t s = t.frame.es; encode(out, &s); } else encode_null(out, regsize[r]); return; }
                    case fs: { if (new_frame_type) { std::uint32_t s = t.frame.fs; encode(out, &s); } else encode_null(out, regsize[r]); return; }
                    case gs: { if (new_frame_type) { std::uint32_t s = t.frame.gs; encode(out, &s); } else encode_null(out, regsize[r]); return; }
                    case eip: encode(out, &t.frame.fault_address.offset); return;
                    default:
                        if (r > reg_max) return;
                        auto* volatile fpu = fpu_context_switcher.get_last_context();
                        switch (r)
                        {
                            case st0: case st1: case st2: case st3: case st4: case st5: case st6: case st7:
                                encode(out, &fpu->st[r - st0], regsize[r]); return;
                            case fctrl: { std::uint32_t s = fpu->fctrl; encode(out, &s); return; }
                            case fstat: { std::uint32_t s = fpu->fstat; encode(out, &s); return; }
                            case ftag:  { std::uint32_t s = fpu->ftag ; encode(out, &s); return; }
                            case fiseg: { std::uint32_t s = fpu->fiseg; encode(out, &s); return; }
                            case fioff: { std::uint32_t s = fpu->fioff; encode(out, &s); return; }
                            case foseg: { std::uint32_t s = fpu->foseg; encode(out, &s); return; }
                            case fooff: { std::uint32_t s = fpu->fooff; encode(out, &s); return; }
                            case fop:   { std::uint32_t s = fpu->fop  ; encode(out, &s); return; }
#                           ifdef HAVE__SSE__
                            case xmm0: case xmm1: case xmm2: case xmm3: case xmm4: case xmm5: case xmm6: case xmm7:
                                encode(out, &fpu->xmm[r - xmm0]); return;
                            case mxcsr: encode(out, &fpu->mxcsr); return;
#                           endif
                            default: encode_null(out, regsize[r]); return;
                        }
                    }
                }
                else
                {
                    auto t_ptr = t.thread.lock();
                    if (not t_ptr or t_ptr->get_state() == thread::detail::starting)
                    {
                        encode_null(out, regsize[r]);
                        return;
                    }
                    auto* reg = thread::detail::thread_details::get_context(t_ptr);
                    auto r_esp = reinterpret_cast<std::uintptr_t>(reg) - sizeof(thread::detail::thread_context);
                    auto r_eip = reg->return_address;
                    switch (r)
                    {
                    case ebx: encode(out, &reg->ebx); return;
                    case ebp: encode(out, &reg->ebp); return;
                    case esi: encode(out, &reg->esi); return;
                    case edi: encode(out, &reg->edi); return;
                    case esp: encode(out, &r_esp); return;
                    case cs: { std::uint32_t s = current_thread->frame.fault_address.segment;  encode(out, &s); return; }
                    case ss: { std::uint32_t s = current_thread->frame.stack.segment; encode(out, &s); return; }
                    case ds: { std::uint32_t s = current_thread->frame.stack.segment; encode(out, &s); return; }
                    case es: { std::uint32_t s = reg->es; encode(out, &s, regsize[r]); return; }
                    case fs: { std::uint32_t s = reg->fs; encode(out, &s, regsize[r]); return; }
                    case gs: { std::uint32_t s = reg->gs; encode(out, &s, regsize[r]); return; }
                    case eip: encode(out, &r_eip); return;
                    default: encode_null(out, regsize[r]);
                    }
                }
            }

            bool setreg(regnum r, const std::string_view& value, std::uint32_t id)
            {
                if (threads.count(id) == 0) return false;
                auto&& t = threads[id];
                if (&t != current_thread) return false; // TODO
                if (debugmsg) std::clog << "set register " << std::hex << r << '=' << value << '\n';
                switch (r)
                {
                case eax:    return reverse_decode(value, &t.reg.eax, regsize[r]);
                case ebx:    return reverse_decode(value, &t.reg.ebx, regsize[r]);
                case ecx:    return reverse_decode(value, &t.reg.ecx, regsize[r]);
                case edx:    return reverse_decode(value, &t.reg.edx, regsize[r]);
                case ebp:    return reverse_decode(value, &t.reg.ebp, regsize[r]);
                case esi:    return reverse_decode(value, &t.reg.esi, regsize[r]);
                case edi:    return reverse_decode(value, &t.reg.edi, regsize[r]);
                case esp:    return reverse_decode(value, &t.frame.stack.offset, regsize[r]);
                case eip:    return reverse_decode(value, &t.frame.fault_address.offset, regsize[r]);
                case eflags: return reverse_decode(value, &t.frame.flags.raw_eflags, regsize[r]);
                case cs:     return reverse_decode(value.substr(0, 4), &t.frame.fault_address.segment, regsize[r]);
                case ss:     return reverse_decode(value.substr(0, 4), &t.frame.stack.segment, regsize[r]);
                case ds: if (new_frame_type) { return reverse_decode(value.substr(0, 4), &t.frame.ds, regsize[r]); } return false;
                case es: if (new_frame_type) { return reverse_decode(value.substr(0, 4), &t.frame.es, regsize[r]); } return false;
                case fs: if (new_frame_type) { return reverse_decode(value.substr(0, 4), &t.frame.fs, regsize[r]); } return false;
                case gs: if (new_frame_type) { return reverse_decode(value.substr(0, 4), &t.frame.gs, regsize[r]); } return false;
                default: if (r > mxcsr) return false;
                    //auto fpu = fpu_context_switcher.get_last_context();
                    switch (r)
                    {
                    default: return false; // TODO
                    }
                }
            }

            void stop_reply(bool force = false, bool async = false)
            {
                auto do_stop_reply = [force, async] (auto&& t, bool report_last = false)
                {
                    if (t.action == thread_info::none and not force) return false;
                    auto no_stop_signal = [] (auto&& t)
                    {
                        if (t.signals.empty()) return true;
                        for (auto&& i : t.signals) if (is_stop_signal(i)) return false;
                        return true;
                    };
                    if (no_stop_signal(t) and report_last) t.signals.insert(t.last_stop_signal);

                    auto t_ptr = t.thread.lock();

                    for (auto i = t.signals.begin(); i != t.signals.end();)
                    {
                        auto signal = *i;
                        if (temp_debugmsg) std::clog << "stop reply for thread 0x" << std::hex << t_ptr->id() << " signal 0x" << signal << ": ";
                        if (not is_stop_signal(signal)
                            or (is_trap_signal(signal) and t.trap_mask > 0))
                        {
                            if (temp_debugmsg) std::clog << "ignored.\n";
                            ++i;
                            continue;
                        }
                        else i = t.signals.erase(i);
                        if (signal == SIGINT) for (auto&&t : threads) t.second.signals.erase(SIGINT);
                        if (temp_debugmsg) std::clog << "handled.\n";

                        if (not thread_events_enabled and (signal == thread_started or signal == thread_finished))
                            continue;

                        t.action = thread_info::none;
                        t.last_stop_signal = signal;

                        std::stringstream s { };
                        s << std::hex << std::setfill('0');
                        if (async) s << "Stop:";
                        if (signal == thread_finished)
                        {
                            s << 'w';
                            if (t_ptr->get_state() == thread::detail::finished) s << "00";
                            else s << "ff";
                            s << ';' << t_ptr->id();
                            send_packet(s.str());
                        }
                        else
                        {
                            s << "T" << std::setw(2) << posix_signal(signal);
                            if (t_ptr->get_state() != thread::detail::starting)
                            {
                                s << eip << ':'; reg(s, eip, t_ptr->id()); s << ';';
                                s << esp << ':'; reg(s, esp, t_ptr->id()); s << ';';
                                s << ebp << ':'; reg(s, ebp, t_ptr->id()); s << ';';
                            }
                            s << "thread:" << t_ptr->id() << ';';
                            if (signal == thread_started)
                            {
                                s << "create:;";
                            }
                            else if (is_trap_signal(signal))
                            {
                                if (signal == watchpoint_hit)
                                {
                                    for (auto&& w : watchpoints)
                                    {
                                        if (w.second.get_state())
                                        {
                                            if (w.second.get_type() == watchpoint::execute) s << "hwbreak:;";
                                            else s << "watch:" << w.first << ";";
                                            break;
                                        }
                                    }
                                }
                                else s << "swbreak:;";
                            }
                            if (async) send_notification(s.str());
                            else send_packet(s.str());
                            query_thread_id = t_ptr->id();
                        }

                        if (signal == all_threads_suspended and supported["no-resumed"] == "+")
                            send_packet("N");

                        return true;
                    }
                    return false;
                };
                bool report_last = false;

            try_harder:
                if (not do_stop_reply(*current_thread, report_last))
                {
                    auto report_other_threads = [&do_stop_reply, &report_last]
                    {
                        for (auto&& t : threads)
                            if (do_stop_reply(t.second), report_last) return true;
                        return false;
                    };
                    if (not report_other_threads() and force)
                    {
                        report_last = true;
                        goto try_harder;
                    }
                }
            }

            [[gnu::hot]] void handle_packet()
            {
                recv_packet();
                current_thread->signals.erase(packet_received);

                std::stringstream s { };
                s << std::hex << std::setfill('0');
                auto& p = packet.front().delim;
                if (p == '?')   // stop reason
                {
                    stop_reply(true);
                }
                else if (p == 'q')  // query
                {
                    auto& q = packet[0];
                    if (q == "Supported")
                    {
                        sent_packets.clear();
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
                            std::uintptr_t begin { 0 }, end { 0 };
                            char c { packet[i][0] };
                            if (c == 'r')
                            {
                                if (i + 1 >= packet.size() or packet[i + 1].delim != ',')
                                {
                                    send_packet("E00");
                                    break;
                                }
                                begin = decode(packet[i].substr(1));
                                end = decode(packet[i + 1]);
                                ++i;
                            }
                            if (i + 1 < packet.size() and packet[i + 1].delim == ':')
                            {
                                auto id = decode(packet[i + 1]);
                                if (threads.count(id)) threads[id].set_action(c, 0, begin, end);
                                ++i;
                            }
                            else
                            {
                                for (auto&& t : threads)
                                {
                                    if (t.second.action == thread_info::none)
                                        t.second.set_action(c, 0, begin, end);
                                }
                            }
                        }
                    }
                    else if (v == "CtrlC")
                    {
                        auto already_stopped = []
                        {
                            for (auto&& t : threads) if (t.second.signals.count(SIGINT)) return true;
                            return false;
                        };
                        send_packet("OK");
                        if (already_stopped()) stop_reply();
                        else
                        {
                            for (auto&& t : threads) t.second.signals.insert(SIGINT);
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
                        if (packet[0][0] == 'g')
                        {
                            if (id == all_threads_id) query_thread_id = current_thread_id;
                            else query_thread_id = id;
                        }
                        else if (packet[0][0] == 'c') control_thread_id = id;
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
                    if (threads.count(query_thread_id))
                    {
                        auto regn = static_cast<regnum>(decode(packet[0]));
                        reg(s, regn, query_thread_id);
                        send_packet(s.str());
                    }
                    else send_packet("E00");
                }
                else if (p == 'P')  // write one register
                {
                    if (setreg(static_cast<regnum>(decode(packet[0])), packet[1], query_thread_id)) send_packet("OK");
                    else send_packet("E00");
                }
                else if (p == 'g')  // read registers
                {
                    if (threads.count(query_thread_id))
                    {
                        for (auto i = eax; i <= gs; ++i)
                            reg(s, i, query_thread_id);
                        send_packet(s.str());
                    }
                    else send_packet("E00");
                }
                else if (p == 'G')  // write registers
                {
                    regnum reg { };
                    std::size_t pos { };
                    bool fail { false };
                    while (pos < packet[0].size())
                    {
                        if (fail |= setreg(reg, packet[0].substr(pos), query_thread_id))
                        {
                            send_packet("E00");
                            break;
                        }
                        pos += regsize[reg] * 2;
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
                    auto id = control_thread_id;
                    auto step_continue = [](auto& t)
                    {
                        if (packet.size() > 0)
                        {
                            std::uintptr_t jmp = decode(packet[0]);
                            if (debugmsg and t.frame.fault_address.offset != jmp) std::clog << "JUMP to 0x" << std::hex << jmp << '\n';
                            t.frame.fault_address.offset = jmp;
                        }
                        t.set_action(packet[0].delim);
                    };
                    if (id == all_threads_id) for (auto&& t : threads) step_continue(t.second);
                    else if (threads.count(id)) step_continue(threads[id]);
                    else send_packet("E00");

                }
                else if (p == 'C' or p == 'S')  // step/continue with signal
                {
                    auto id = control_thread_id;
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
                    if (id == all_threads_id) for (auto&& t : threads) step_continue(t.second);
                    else if (threads.count(id)) step_continue(threads[id]);
                    else send_packet("E00");
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
                            return;
                        }
                        set_breakpoint(addr);
                        send_packet("OK");
                    }
                    else            // set watchpoint
                    {
                        watchpoint::watchpoint_type w;
                        if (z == '1') w = watchpoint::execute;
                        else if (z == '2') w = watchpoint::write;
                        else if (z == '3') w = watchpoint::read_write;
                        else if (z == '4') w = watchpoint::read_write;
                        else
                        {
                            send_packet("");
                            return;
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
                    if (debugmsg) std::clog << "KILL signal received.";
                    for (auto&&t : threads) t.second.set_action('c');
                    simulate_call(&current_thread->frame, dpmi::detail::kill);
                    s << "X" << std::setw(2) << posix_signal(current_thread->last_stop_signal);
                    send_packet(s.str());
                    uninstall_gdb_interface();
                }
                else send_packet("");   // unknown packet
            }

            [[gnu::hot]] bool handle_exception(exception_num exc, cpu_registers* r, exception_frame* f, bool)
            {
                if (debugmsg) std::clog << "entering exception 0x" << std::hex << exc << " from 0x" << f->fault_address.offset << '\n';
                if (not debug_mode)
                {
                    if (debugmsg) std::cerr << "already killed!\n";
                    return false;
                }

                if (exc == 0x03) f->fault_address.offset -= 1;

                auto catch_exception = []
                {
                    std::cerr << "Exception occured while communicating with GDB.\n";
                    std::cerr << "caused by this packet: " << raw_packet_string << '\n';
                    std::cerr << std::boolalpha << "good=" << gdb->good() << " bad=" << gdb->bad() << " fail=" << gdb->fail() << " eof=" << gdb->eof() << '\n';
                    do { } while (true);
                };

                auto leave = [exc, f]
                {
                    for (auto&& w : watchpoints) w.second.reset();
                    enable_all_breakpoints();
                    if (*reinterpret_cast<byte*>(f->fault_address.offset) == 0xcc)  // don't resume on a breakpoint
                    {
                        if (disable_breakpoint(f->fault_address.offset))
                            f->flags.trap = true;   // trap on next instruction to re-enable
                        else f->fault_address.offset += 1;  // hardcoded breakpoint, safe to skip
                    }

                    if (debugmsg) std::clog << "leaving exception 0x" << std::hex << exc << ", resuming at 0x" << f->fault_address.offset << '\n';
                };

                auto clear_trap_signals = []
                {
                    for (auto i = current_thread->signals.begin(); i != current_thread->signals.end();)
                    {
                        if (is_trap_signal(*i)) i = current_thread->signals.erase(i);
                        else ++i;
                    }
                };

                if (__builtin_expect(f->fault_address.segment != ring3_cs and f->fault_address.segment != ring0_cs, false))
                {
                    if (exc == exception_num::trap) return true; // keep stepping until we get back to our own code
                    std::cerr << "Can't debug this! CS is neither 0x" << std::hex << ring3_cs << " nor 0x" << ring0_cs << ".\n";
                    std::cerr << cpu_exception { exc, r, f, new_frame_type }.what() << '\n';
                    return false;
                }

                if (__builtin_expect(debugger_reentry, false))
                {
                    if (exc == 0x01 or exc == 0x03)
                    {   // breakpoint in debugger code, ignore
                        if (debugmsg) std::clog << "reentry caused by breakpoint, ignoring.\n";
                        leave();
                        f->flags.trap = false;
                        return true;
                    }
                    if (debugmsg) std::clog << "debugger re-entry! " << *static_cast<new_exception_frame*>(f) << *r;
                    throw cpu_exception { exc, r, f, new_frame_type };
                }

                try
                {
                    debugger_reentry = true;
                    populate_thread_list();
                    if (packet_available()) current_thread->signals.insert(packet_received);

                    if (exc == exception_num::breakpoint and current_signal != -1)
                    {
                        if (debugmsg) std::clog << "break with signal 0x" << std::hex << current_signal << '\n';
                        current_thread->signals.insert(current_signal);
                        current_signal = -1;
                    }
                    else if (exc == exception_num::trap and [] {for (auto&& w : watchpoints) { if (w.second.get_state()) return true; } return false; }())
                    {
                        current_thread->signals.insert(watchpoint_hit);
                    }
                    else current_thread->signals.insert(exc);

                    if (current_thread->signals.count(trap_unmasked))
                    {
                        auto size = current_thread->signals.size();
                        clear_trap_signals();
                        if (size != current_thread->signals.size()) current_thread->signals.insert(continued); // resume with SIGCONT so gdb won't get confused
                        current_thread->signals.erase(trap_unmasked);
                    }

                    if (current_thread->trap_mask > 0)
                    {
                        if (all_benign_signals(current_thread))
                        {
                            if (debugmsg) std::clog << "trap masked at 0x" << std::hex << f->fault_address.offset << "\n";
                        }
                        else
                        {
                            current_thread->trap_mask = 0;     // serious fault occured, undo trap masks
                        }
                    }

                    if (debugmsg) std::clog << *static_cast<new_exception_frame*>(f) << *r;
                    if (new_frame_type) current_thread->frame = *static_cast<new_exception_frame*>(f);
                    else static_cast<old_exception_frame&>(current_thread->frame) = *f;
                    current_thread->reg = *r;

                    for (auto&&t : threads)
                    {
                        if (t.second.action == thread_info::none)
                        {
                            if (thread_events_enabled) t.second.set_action('t');
                            else t.second.set_action('c');

                            if (t.second.thread.lock()->get_state() == thread::detail::starting)
                                t.second.signals.insert(thread_started);
                        }

                        if (current_thread->signals.count(thread_switched) and t.second.action == thread_info::stop)
                            t.second.signals.insert(thread_suspended);
                    }

                    current_thread->signals.erase(thread_switched);

                    if (exc == exception_num::trap and current_thread->signals.count(watchpoint_hit) == 0 and
                        current_thread->action == thread_info::step_range and
                        current_thread->frame.fault_address.offset >= current_thread->step_range_begin and
                        current_thread->frame.fault_address.offset <= current_thread->step_range_end)
                    {
                        if (debugmsg) std::clog << "range step until 0x" << std::hex << current_thread->step_range_end;
                        if (debugmsg) std::clog << ", now at 0x" << f->fault_address.offset << '\n';
                        clear_trap_signals();
                    }

                    if (debugmsg)
                    {
                        std::clog << "signals:";
                        for (auto&& s : current_thread->signals) std::clog << " 0x" << std::hex << s;
                        std::clog << '\n';
                    }

                    if (temp_debugmsg) std::clog << "sending stop reply.\n";
                    stop_reply();

                    if (config::enable_gdb_interrupts and current_thread->frame.flags.interrupt) asm("sti");

                    auto cant_continue = []
                    {
                        for (auto&& t : threads)
                            if (t.second.thread.lock()->is_running() and
                                t.second.action == thread_info::none) return true;
                        return false;
                    };
                    if (temp_debugmsg) std::clog << "entering main loop.\n";
                    do
                    {
                        try
                        {
                            if (packet_available()) handle_packet();
                        }
                        catch (...)
                        {
                            // TODO: determine action based on last packet / signal
                            if (not replied) send_packet("E04"); // last command caused another exception (most likely page fault after a request to read memory)
                        }
                        recv_ack();
                    } while (cant_continue());
                    if (temp_debugmsg) std::clog << "leaving main loop.\n";

                    while (sent_packets.size() > 0 and debug_mode) recv_ack();
                }
                catch (const std::exception& e) { print_exception(e); catch_exception(); }
                catch (...) { catch_exception(); }
                asm("cli");

                if (new_frame_type) *f = static_cast<new_exception_frame&>(current_thread->frame);
                else *static_cast<old_exception_frame*>(f) = current_thread->frame;
                *r = current_thread->reg;

                leave();
                debugger_reentry = false;

                return current_thread->do_action();
            }

            void notify_gdb_thread_event(debug_signals e)
            {
                if (thread_events_enabled) break_with_signal(e);
            }

            extern "C" void csignal(int signal)
            {
                break_with_signal(signal);
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
                    if (debugger_reentry) return;
                    if (packet_available()) break_with_signal(packet_received);
                }, dpmi::always_call);

                {
                    exception_handler check_frame_type { 3, [](auto*, auto*, bool t)
                    { 
                        new_frame_type = t;
                        return true; 
                    } };
                    asm("int 3;");
                }

                for (auto&& s : { SIGHUP, SIGABRT, SIGTERM, SIGKILL, SIGQUIT, SIGILL, SIGINT })
                    signal_handlers[s] = std::signal(s, csignal);

                auto install_exception_handler = [](auto&& e) { exception_handlers[e] = std::make_unique<exception_handler>(e, [e](auto* r, auto* f, bool t) { return handle_exception(e, r, f, t); }); };

                for (auto e = 0x00; e <= 0x0e; ++e)
                    install_exception_handler(e);

                serial_irq->set_irq(cfg.irq);
                serial_irq->enable();

                capabilities c { };
                if (!c.supported) return;
                if (std::strncmp(c.vendor_info.name, "HDPMI", 5) != 0) return;  // TODO: figure out if other hosts support these too
                for (auto&& e : { 0x10, 0x11, 0x12, 0x13, 0x14, 0x1e })
                    install_exception_handler(e);
            }

            void uninstall_gdb_interface()
            {
                debug_mode = false;
                serial_irq.reset();
                watchpoints.clear();
                for (auto&& bp : breakpoints) *reinterpret_cast<byte*>(bp.first) = bp.second;
                for (auto&& e : exception_handlers) e.reset();
                for (auto&& s : signal_handlers) std::signal(s.first, s.second);
            }

            void notify_gdb_exit(byte result)
            {
                std::stringstream s { };
                s << std::hex << std::setfill('0');
                s << "W" << std::setw(2) << static_cast<std::uint32_t>(result);
                send_packet(s.str());
                uninstall_gdb_interface();
            }
        }

        trap_mask::trap_mask() noexcept // TODO: ideally this should treat interrupts as separate 'threads'
        {
            if (not debug()) { failed = true; return; }
            if (detail::debugger_reentry) { failed = true; return; }
            ++detail::threads[jw::thread::detail::scheduler::get_current_thread_id()].trap_mask;
        }

        trap_mask::~trap_mask() noexcept
        {
            FORCE_FRAME_POINTER;
            if (failed) return;
            auto& t = detail::threads[jw::thread::detail::scheduler::get_current_thread_id()];
            t.trap_mask = std::max(t.trap_mask - 1, 0l);
            if (t.trap_mask == 0 and [&t]
            {
                for (auto&& s : t.signals) if (detail::is_trap_signal(s)) return true;
                return false;
            }()) break_with_signal(detail::trap_unmasked);
        }
#       else
        namespace detail
        {
            void notify_gdb_thread_event(debug_signals) { }
        }
#       endif

        _Unwind_Reason_Code unwind_print_trace(_Unwind_Context* c, void*)
        {
            std::clog << " --> " << std::hex << std::noshowbase << std::setfill(' ') << std::setw(11) << _Unwind_GetIP(c);
            return _URC_NO_REASON;
        }

        void print_backtrace() noexcept
        {
            std::clog << "Backtrace  ";
            _Unwind_Backtrace(unwind_print_trace, nullptr);
            std::clog << '\n';
        }
    }
}
