/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2017 - 2024 J.W. Jagersma, see COPYING.txt for details    */

#include <array>
#include <cstring>
#include <memory>
#include <csignal>
#include <cstdlib>
#include <string_view>
#include <set>
#include <unwind.h>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>
#include <jw/main.h>
#include <jw/dpmi/fpu.h>
#include <jw/dpmi/dpmi.h>
#include <jw/debug/debug.h>
#include <jw/dpmi/cpu_exception.h>
#include <jw/dpmi/detail/selectors.h>
#include <jw/debug/detail/signals.h>
#include <jw/io/rs232.h>
#include <jw/alloc.h>
#include <jw/dpmi/ring0.h>
#include <jw/allocator_adaptor.h>
#include <jw/thread.h>
#include <jw/dpmi/async_signal.h>
#include "jwdpmi_config.h"

using namespace std::literals;
using namespace jw::dpmi;
using namespace jw::dpmi::detail;

#ifndef NDEBUG
namespace jw::debug::detail
{
    static bool is_fault_signal(std::int32_t) noexcept;
    static void uninstall_gdb_interface();

    const bool debugmsg = config::enable_gdb_debug_messages;

    volatile bool debugger_reentry { false };
    bool debug_mode { false };
    volatile int current_signal { -1 };
    bool thread_events_enabled { false };
    exception_info current_exception;

    using resource_type = locked_pool_resource;

    template<typename T = std::byte>
    using allocator = default_constructing_allocator_adaptor<monomorphic_allocator<resource_type, T>>;

    resource_type memres { 1_MB };

    using string = std::basic_string<char, std::char_traits<char>, allocator<char>>;

    std::pmr::map<std::pmr::string, std::pmr::string> supported { &memres };
    std::pmr::map<std::uintptr_t, watchpoint> watchpoints { &memres };
    std::pmr::map<std::uintptr_t, std::byte> breakpoints { &memres };
    std::map<int, void(*)(int)> signal_handlers { };

    std::array<std::unique_ptr<exception_handler>, 0x20> exception_handlers;
    std::optional<io::rs232_stream> gdb;
    std::unique_ptr<dpmi::irq_handler> serial_irq;
    std::optional<dpmi::async_signal> irq_signal;

    struct packet_string : public std::string_view
    {
        char delim;
        template <typename T, typename U>
        packet_string(T&& str, U&& delimiter): std::string_view(std::forward<T>(str)), delim(std::forward<U>(delimiter)) { }
        using std::string_view::operator=;
        using std::string_view::basic_string_view;
    };

    std::deque<string, allocator<string>> sent_packets { &memres };
    string raw_packet_string { &memres };
    std::deque<packet_string, allocator<packet_string>> packet { &memres };
    bool replied { false };

    struct thread_info;
    using thread_id = jw::detail::thread_id;
    constexpr thread_id main_thread_id = jw::detail::thread::main_thread_id;
    constexpr thread_id all_threads_id { 0 };
    thread_id current_thread_id { 1 };
    thread_id query_thread_id { 1 };
    thread_id control_thread_id { all_threads_id };
    thread_info* current_thread { nullptr };

    struct thread_info
    {
        jw::detail::thread* thread;
        std::pmr::set<std::int32_t> signals { &memres };
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

        std::uintptr_t eip() const noexcept
        {
            if (current_thread == this)
                return current_exception.frame->fault_address.offset;
            else
                return thread->get_context()->return_address;
        }

        void jmp(std::uintptr_t dst) noexcept
        {
            if (current_thread == this)
                current_exception.frame->fault_address.offset = dst;
            else
                thread->get_context()->return_address = dst;
        }

        void trap(bool t) noexcept
        {
            if (current_thread == this)
                current_exception.frame->flags.trap = t;
            else
                thread->get_context()->flags.trap = t;
        };

        void set_action(char a, std::uintptr_t rbegin = 0, std::uintptr_t rend = 0)
        {
            thread->resume();
            if (a == 'c')  // continue
            {
                trap(false);
                action = cont;
            }
            else if (a == 's')  // step
            {
                trap(trap_mask == 0);
                action = step;
            }
            else if (a == 'C')  // continue with signal
            {
                trap(false);
                if (not is_fault_signal(last_stop_signal)) action = cont;
                else action = cont_sig;
            }
            else if (a == 'S')  // step with signal
            {
                trap(trap_mask == 0);
                if (not is_fault_signal(last_stop_signal)) action = step;
                else action = step_sig;
            }
            else if (a == 'r')   // step with range
            {
                trap(trap_mask == 0);
                step_range_begin = rbegin;
                step_range_end = rend;
                action = step_range;
            }
            else if (a == 't')   // stop
            {
                thread->suspend();
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

    std::pmr::map<thread_id, thread_info> threads { &memres };

    static void populate_thread_list()
    {
        for (auto i = threads.begin(); i != threads.end();)
        {
            if (jw::detail::scheduler::get_thread(i->first) == nullptr) i = threads.erase(i);
            else ++i;
        }
        for (auto& t : jw::detail::scheduler::all_threads())
        {
            threads[t.id].thread = const_cast<jw::detail::thread*>(&t);
        }
        current_thread_id = jw::detail::scheduler::current_thread_id();
        threads[current_thread_id].thread = jw::detail::scheduler::current_thread();
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

    constexpr std::array<std::string_view, 41> regname
    {
        "eax", "ecx", "edx", "ebx",
        "esp", "ebp", "esi", "edi",
        "eip", "eflags",
        "cs", "ss", "ds", "es", "fs", "gs",
        "st0", "st1", "st2", "st3", "st4", "st5", "st6", "st7",
        "fctrl", "fstat", "ftag", "fiseg", "fioff", "foseg", "fooff", "fop",
        "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
        "mxcsr"
    };

    constexpr auto reg_max = regnum::mxcsr;

    static std::uint32_t posix_signal(std::int32_t exc) noexcept
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

    static bool is_fault_signal(std::int32_t exc) noexcept
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

    static bool is_stop_signal(std::int32_t exc) noexcept
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

    static bool is_trap_signal(std::int32_t exc) noexcept
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

    static bool is_benign_signal(std::int32_t exc) noexcept
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

    static bool all_benign_signals(auto* t)
    {
        for (auto&& s : t->signals)
            if (not is_benign_signal(s)) return false;
        return true;
    }

    static void set_breakpoint(std::uintptr_t at)
    {
        auto* ptr = reinterpret_cast<std::byte*>(at);
        breakpoints.try_emplace(at, *ptr);
        *ptr = std::byte { 0xcc };
    }

    static bool clear_breakpoint(std::uintptr_t at)
    {
        auto* ptr = reinterpret_cast<std::byte*>(at);
        const auto i = breakpoints.find(at);
        if (i != breakpoints.end())
        {
            *ptr = i->second;
            breakpoints.erase(i);
            return true;
        }
        else return false;
    }

    static bool disable_breakpoint(std::uintptr_t at)
    {
        auto* ptr = reinterpret_cast<std::byte*>(at);
        const auto i = breakpoints.find(at);
        if (i != breakpoints.end())
        {
            *ptr = i->second;
            return true;
        }
        else return false;
    }

    static void enable_all_breakpoints()
    {
        for (auto&& bp : breakpoints)
            *reinterpret_cast<std::byte*>(bp.first) = std::byte { 0xcc };
    }

    // Decode big-endian hex string
    static auto decode(const std::string_view& in)
    {
        std::uint32_t result { };
        if (in[0] == '-') return all_threads_id;
        for (const auto c : in)
        {
            result <<= 4;
            if (c >= 'a' and c <= 'f') result |= 10 + c - 'a';
            else if (c >= '0' and c <= '9') result |= c - '0';
            else throw std::invalid_argument { "decode() failed: "s + in.data() };
        }
        return result;
    }

    // Decode little-endian hex string
    template <typename T>
    static bool reverse_decode(const std::string_view& in, T* out, std::size_t len = sizeof(T))
    {
        len = std::min(len, in.size() / 2);
        auto ptr = reinterpret_cast<byte*>(out);
        for (std::size_t i = 0; i < len; ++i)
        {
            ptr[i] = decode(in.substr(i * 2, 2));
        }
        return true;
    }

    // Encode little-endian hex string
    template <typename T>
    static void encode(string& out, T* in, std::size_t len = sizeof(T))
    {
        constexpr char hex[] = "0123456789abcdef";
        auto* const ptr = reinterpret_cast<const volatile byte*>(in);
        const auto size = out.size();
        out.resize(size + len * 2);
        auto it = out.data() + size;

        for (std::size_t i = 0; i < len; ++i)
        {
            const byte b = ptr[i];
            const byte hi = b >> 4;
            const byte lo = b & 0xf;
            *it++ = hex[hi];
            *it++ = hex[lo];
        }
    }

    static void encode_null(string& out, std::size_t len)
    {
        out.append(len * 2, 'x');
    }

    static std::uint32_t checksum(const std::string_view& s)
    {
        std::uint8_t r { 0 };
        for (auto&& c : s) r += c;
        return r;
    }

    static bool packet_available()
    {
        return gdb->rdbuf()->in_avail() != 0;
    }

    // not used
    void send_notification(const std::string_view& output)
    {
        if (config::enable_gdb_protocol_dump) fmt::print(stderr, "note --> \"{}\"\n", output);
        fmt::print(*gdb, "%{}#{:0>2x}", output, checksum(output));
    }

    static void send_packet(std::string_view output)
    {
        if (config::enable_gdb_protocol_dump) fmt::print(stderr, "send --> \"{}\"\n", output);

        static string buf { &memres };
        buf.resize(output.size());
        auto* p = buf.data();

        std::size_t i = 0;
        while (i < output.size())
        {
            const auto ch = output[i];
            auto j = output.find_first_not_of(ch, i);
            if (j == output.npos)
                j = output.size();
            auto count = j - i;
            if (count > 3)
            {
                count = std::min(count, std::size_t { 98 }); // above 98, RLE byte would be non-printable
                if (count == 7 or count == 8) count = 6;     // RLE byte can't be '#' or '$'
                *p++ = ch;
                *p++ = '*';
                *p++ = static_cast<char>(count + 28);
            }
            else
            {
                std::memset(p, ch, count);
                p += count;
            }
            i += count;
        }

        const std::string_view rle { buf.data(), p };
        const auto sum = checksum(rle);
        fmt::print(*gdb, "${}#{:0>2x}", rle, sum);
        sent_packets.emplace_back(output);
        replied = true;
    }

    static void recv_ack()
    {
        *gdb << std::flush;
        if (gdb->rdbuf()->in_avail()) switch (gdb->peek())
        {
        case '-':
            fmt::print(stderr, "NACK --> {}\n", sent_packets.back());
            if (sent_packets.size() > 0) send_packet(sent_packets.back());
            [[fallthrough]];
        case '+':
            if (sent_packets.size() > 0) sent_packets.pop_back();
            gdb->get();
        }
    }

    static void recv_packet()
    {
        char sum_data[2];
        std::string_view sum { sum_data, 2 };
        bool bad = false;

    retry:
        gdb->clear();
        try
        {
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
            sum_data[0] = gdb->get();
            sum_data[1] = gdb->get();
        }
        catch (const std::exception& e)
        {
            fmt::print(stderr, "Error while receiving gdb packet: {}\n", e.what());
            bad |= gdb->bad();
            if (gdb->rdbuf()->in_avail() != -1)
            {
                fmt::print(stderr, "Received so far: \"{}\"\n", raw_packet_string);
                if (bad)
                    fmt::print(stderr, "Malformed character: \'{}\'\n", gdb->get());
                gdb->put('-');
                bad = false;
            }
            goto retry;
        }

        if (decode(sum) == checksum(raw_packet_string)) *gdb << '+';
        else
        {
            fmt::print(stderr, "Bad checksum: \"{}\": {}, calculated: {:0>2x}\n",
                        raw_packet_string, sum, checksum(raw_packet_string));
            gdb->put('-');
            goto retry;
        }

    parse:
        if (config::enable_gdb_protocol_dump) fmt::print(stderr, "recv <-- \"{}\"\n", raw_packet_string);
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

    template<typename T>
    static void fpu_reg(string& out, regnum reg, const T* fpu)
    {
        assume(reg >= st0);
        switch (reg)
        {
            case st0: case st1: case st2: case st3: case st4: case st5: case st6: case st7:
                return encode(out, &fpu->st[reg - st0], regsize[reg]);
            case fctrl: { std::uint32_t s = fpu->fctrl; return encode(out, &s); }
            case fstat: { std::uint32_t s = fpu->fstat; return encode(out, &s); }
            case ftag:  { std::uint32_t s = fpu->ftag ; return encode(out, &s); }
            case fiseg: { std::uint32_t s = fpu->fiseg; return encode(out, &s); }
            case fioff: { std::uint32_t s = fpu->fioff; return encode(out, &s); }
            case foseg: { std::uint32_t s = fpu->foseg; return encode(out, &s); }
            case fooff: { std::uint32_t s = fpu->fooff; return encode(out, &s); }
            case fop:   { std::uint32_t s = fpu->fop  ; return encode(out, &s); }
            default:
                if constexpr (std::is_same_v<T, fxsave_data>)
                {
                    if (reg == mxcsr) return encode(out, &fpu->mxcsr);
                    else return encode(out, &fpu->xmm[reg - xmm0]);
                }
                else return encode_null(out, regsize[reg]);
        }
    }

    static void reg(string& out, regnum reg, std::uint32_t id)
    {
        if (threads.count(id) == 0)
        {
            encode_null(out, regsize[reg]);
            return;
        }
        if (reg > reg_max) [[unlikely]] return;

        auto&& t = threads[id];
        if (&t == current_thread)
        {
            auto* const r = current_exception.registers;
            auto* const f = current_exception.frame;
            auto* const d10f = static_cast<dpmi10_exception_frame*>(current_exception.frame);
            const bool dpmi10_frame = current_exception.is_dpmi10_frame;

            switch (reg)
            {
            case eax: return encode(out, &r->eax);
            case ebx: return encode(out, &r->ebx);
            case ecx: return encode(out, &r->ecx);
            case edx: return encode(out, &r->edx);
            case ebp: return encode(out, &r->ebp);
            case esi: return encode(out, &r->esi);
            case edi: return encode(out, &r->edi);
            case esp: return encode(out, &f->stack.offset);
            case eflags: return encode(out, &f->raw_eflags);
            case cs: { std::uint32_t s = f->fault_address.segment; return encode(out, &s); }
            case ss: { std::uint32_t s = f->stack.segment; return encode(out, &s); }
            case ds: { if (dpmi10_frame) { std::uint32_t s = d10f->ds; return encode(out, &s); } else return encode_null(out, regsize[reg]); }
            case es: { if (dpmi10_frame) { std::uint32_t s = d10f->es; return encode(out, &s); } else return encode_null(out, regsize[reg]); }
            case fs: { if (dpmi10_frame) { std::uint32_t s = d10f->fs; return encode(out, &s); } else return encode_null(out, regsize[reg]); }
            case gs: { if (dpmi10_frame) { std::uint32_t s = d10f->gs; return encode(out, &s); } else return encode_null(out, regsize[reg]); }
            case eip: return encode(out, &f->fault_address.offset);
            default:
                auto* const fpu = interrupt_id::get()->fpu;
                if (fpu == nullptr) return encode_null(out, regsize[reg]);
                switch (fpu_registers::type())
                {
                case fpu_registers_type::fsave:  return fpu_reg(out, reg, &fpu->fsave);
                case fpu_registers_type::fxsave: return fpu_reg(out, reg, &fpu->fxsave);
                default: __builtin_unreachable();
                }
            }
        }
        else
        {
            auto* t_ptr = t.thread;
            if (not t_ptr or t_ptr->get_state() == jw::detail::thread::starting)
                return encode_null(out, regsize[reg]);
            auto* r = t_ptr->get_context();
            std::uint32_t r_esp = reinterpret_cast<std::uintptr_t>(r) - sizeof(jw::detail::thread_context);
            std::uint32_t r_eip = r->return_address;
            switch (reg)
            {
            case ebx: return encode(out, &r->ebx);
            case ebp: return encode(out, &r->ebp);
            case esi: return encode(out, &r->esi);
            case edi: return encode(out, &r->edi);
            case esp: return encode(out, &r_esp);
            case cs: { std::uint32_t s = main_cs; return encode(out, &s); }
            case ss:
            case ds:
            case es: { std::uint32_t s = main_ds; return encode(out, &s); }
            case fs: { std::uint32_t s = r->fs; return encode(out, &s, regsize[reg]); }
            case gs: { std::uint32_t s = r->gs; return encode(out, &s, regsize[reg]); }
            case eip: return encode(out, &r_eip);
            default: return encode_null(out, regsize[reg]);
            }
        }
    }

    template<typename T>
    static bool set_fpu_reg(regnum reg, const std::string_view& value, T* fpu)
    {
        assume(reg >= st0);
        switch (reg)
        {
        case st0: case st1: case st2: case st3: case st4: case st5: case st6: case st7:
            return reverse_decode(value, &fpu->st[reg - st0], regsize[reg]);
        case fctrl: { return reverse_decode(value, &fpu->fctrl, regsize[reg]); }
        case fstat: { return reverse_decode(value, &fpu->fstat, regsize[reg]); }
        case ftag:  { return reverse_decode(value, &fpu->ftag,  regsize[reg]); }
        case fiseg: { return reverse_decode(value, &fpu->fiseg, regsize[reg]); }
        case fioff: { return reverse_decode(value, &fpu->fioff, regsize[reg]); }
        case foseg: { return reverse_decode(value, &fpu->foseg, regsize[reg]); }
        case fooff: { return reverse_decode(value, &fpu->fooff, regsize[reg]); }
        case fop:   { return reverse_decode(value, &fpu->fop,   regsize[reg]); }
        default:
            if constexpr (std::is_same_v<T, fxsave_data>)
            {
                if (reg == mxcsr) return reverse_decode(value, &fpu->mxcsr, regsize[reg]);
                else return reverse_decode(value, &fpu->xmm[reg - xmm0], regsize[reg]);
            }
            else return false;
        }
    }

    static bool setreg(regnum reg, const std::string_view& value, std::uint32_t id)
    {
        if (threads.count(id) == 0) return false;
        auto&& t = threads[id];
        if (&t == current_thread)
        {
            auto* const r = current_exception.registers;
            auto* const f = current_exception.frame;
            auto* const d10f = static_cast<dpmi10_exception_frame*>(current_exception.frame);
            const bool dpmi10_frame = current_exception.is_dpmi10_frame;
            if (debugmsg) fmt::print(stderr, "set register {}={}\n", regname[reg], value);
            switch (reg)
            {
            case eax:    return reverse_decode(value, &r->eax, regsize[reg]);
            case ebx:    return reverse_decode(value, &r->ebx, regsize[reg]);
            case ecx:    return reverse_decode(value, &r->ecx, regsize[reg]);
            case edx:    return reverse_decode(value, &r->edx, regsize[reg]);
            case ebp:    return reverse_decode(value, &r->ebp, regsize[reg]);
            case esi:    return reverse_decode(value, &r->esi, regsize[reg]);
            case edi:    return reverse_decode(value, &r->edi, regsize[reg]);
            case esp:    return reverse_decode(value, &f->stack.offset, regsize[reg]);
            case eip:    return reverse_decode(value, &f->fault_address.offset, regsize[reg]);
            case eflags: return reverse_decode(value, &f->raw_eflags, regsize[reg]);
            case cs:     return reverse_decode(value.substr(0, 4), &f->fault_address.segment, 2);
            case ss:     return reverse_decode(value.substr(0, 4), &f->stack.segment, 2);
            case ds: if (dpmi10_frame) { return reverse_decode(value.substr(0, 4), &d10f->ds, 2); } else return false;
            case es: if (dpmi10_frame) { return reverse_decode(value.substr(0, 4), &d10f->es, 2); } else return false;
            case fs: if (dpmi10_frame) { return reverse_decode(value.substr(0, 4), &d10f->fs, 2); } else return false;
            case gs: if (dpmi10_frame) { return reverse_decode(value.substr(0, 4), &d10f->gs, 2); } else return false;
            default:
                auto* const fpu = interrupt_id::get()->fpu;
                if (fpu == nullptr) return false;
                switch (fpu_registers::type())
                {
                case fpu_registers_type::fsave:  return set_fpu_reg(reg, value, &fpu->fsave);
                case fpu_registers_type::fxsave: return set_fpu_reg(reg, value, &fpu->fxsave);
                default: __builtin_unreachable();
                }
            }
        }
        else
        {
            auto* const r = t.thread->get_context();
            if (debugmsg) fmt::print(stderr, "set thread {:d} register {}={}\n", id, regname[reg], value);
            switch (reg)
            {
            case ebx:    return reverse_decode(value, &r->ebx, regsize[reg]);
            case ebp:    return reverse_decode(value, &r->ebp, regsize[reg]);
            case esi:    return reverse_decode(value, &r->esi, regsize[reg]);
            case edi:    return reverse_decode(value, &r->edi, regsize[reg]);
            case eip:    return reverse_decode(value, &r->return_address, regsize[reg]);
            case eflags: return reverse_decode(value, &r->flags, regsize[reg]);
            case fs:     return reverse_decode(value.substr(0, 4), &r->fs, 2);
            case gs:     return reverse_decode(value.substr(0, 4), &r->gs, 2);
            default: return false;
            }
        }
    }

    static void stop_reply(bool force = false, bool async = false)
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

            auto* t_ptr = t.thread;

            for (auto i = t.signals.begin(); i != t.signals.end();)
            {
                auto signal = *i;
                if (not is_stop_signal(signal)
                    or (is_trap_signal(signal) and t.trap_mask > 0))
                {
                    ++i;
                    continue;
                }
                else i = t.signals.erase(i);
                if (signal == SIGINT) for (auto&&t : threads) t.second.signals.erase(SIGINT);

                if (not thread_events_enabled and (signal == thread_started or signal == thread_finished))
                    continue;

                t.action = thread_info::none;
                t.last_stop_signal = signal;

                static string str { &memres };
                str.clear();

                auto it = [] { return std::back_inserter(str); };

                if (async) str += "Stop:"sv;
                if (signal == thread_finished)
                {
                    fmt::format_to(it(), "w{};{:x}",
                        t_ptr->get_state() == jw::detail::thread::finished ? "00"sv : "ff"sv, t_ptr->id);
                    send_packet(str);
                }
                else
                {
                    fmt::format_to(std::back_inserter(str), "T{:0>2x}", posix_signal(signal));
                    if (t_ptr->get_state() != jw::detail::thread::starting)
                    {
                        str += "8:"; reg(str, eip, t_ptr->id); str += ';';
                        str += "4:"; reg(str, esp, t_ptr->id); str += ';';
                        str += "5:"; reg(str, ebp, t_ptr->id); str += ';';
                    }
                    fmt::format_to(it(), "thread:{:x};", t_ptr->id);
                    if (signal == thread_started)
                    {
                        str += "create:;"sv;
                    }
                    else if (is_trap_signal(signal))
                    {
                        if (signal == watchpoint_hit)
                        {
                            for (auto&& w : watchpoints)
                            {
                                if (w.second.get_state())
                                {
                                    if (w.second.get_type() == watchpoint::execute) str += "hwbreak:;"sv;
                                    else fmt::format_to(it(), "watch:{:x};", w.first);
                                    break;
                                }
                            }
                        }
                        else str += "swbreak:;"sv;
                    }
                    if (async) send_notification(str);
                    else send_packet(str);
                    query_thread_id = t_ptr->id;
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
                    if (do_stop_reply(t.second, report_last)) return true;
                return false;
            };
            if (not report_other_threads() and force)
            {
                report_last = true;
                goto try_harder;
            }
        }
    }

    [[gnu::hot]] static void handle_packet()
    {
        recv_packet();
        current_thread->signals.erase(packet_received);

        static string str { &memres };
        str.clear();
        auto it = std::back_inserter(str);

        //s << std::hex << std::setfill('0');
        auto& p = packet.front().delim;
        if (p == '?')   // stop reason
        {
            stop_reply(true);
        }
        else if (p == 'q')  // query
        {
            auto& q = packet[0];
            if (q == "Supported"sv)
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
                send_packet("PacketSize=399;swbreak+;hwbreak+;QThreadEvents+;no-resumed+"sv);
            }
            else if (q == "Attached"sv) send_packet("0");
            else if (q == "C"sv)
            {
                it = fmt::format_to(it, "QC{:x}", current_thread_id);
                send_packet(str);
            }
            else if (q == "fThreadInfo"sv)
            {
                *it++ = 'm';
                for (auto&& t : threads)
                {
                    it = fmt::format_to(it, "{:x},", t.first);
                }
                send_packet(str);
            }
            else if (q == "sThreadInfo"sv) send_packet("l");
            else if (q == "ThreadExtraInfo"sv)
            {
                using namespace jw::detail;
                static string msg { &memres };
                msg.clear();
                auto id = decode(packet[1]);
                if (threads.count(id))
                {
                    auto* t = threads[id].thread;
                    fmt::format_to(std::back_inserter(msg), "{}{}: ",
                                    t->get_name(), id == current_thread_id ? " (*)"sv : ""sv);
                    switch (t->get_state())
                    {
                    case jw::detail::thread::starting:    msg += "Starting"sv;    break;
                    case jw::detail::thread::running:     msg += "Running"sv;     break;
                    case jw::detail::thread::finishing:   msg += "Finishing"sv;   break;
                    case jw::detail::thread::finished:    msg += "Finished"sv;    break;
                    }
                    if (t->is_suspended()) msg += " (suspended)"sv;
                    if (t->is_canceled()) msg += " (canceled)"sv;
                }
                else msg = "invalid thread"sv;
                encode(str, msg.c_str(), msg.size());
                send_packet(str);
            }
            else send_packet("");
        }
        else if (p == 'Q')
        {
            auto& q = packet[0];
            if (q == "ThreadEvents"sv)
            {
                thread_events_enabled = packet[1][0] - '0';
                send_packet("OK");
            }
            else send_packet("");
        }
        else if (p == 'v')
        {
            auto& v = packet[0];
            if (v == "Stopped"sv)
            {
                stop_reply(true);
            }
            else if (v == "Cont?"sv)
            {
                send_packet("vCont;s;S;c;C;t;r"sv);
            }
            else if (v == "Cont"sv)
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
                        if (threads.count(id)) threads[id].set_action(c, begin, end);
                        ++i;
                    }
                    else
                    {
                        for (auto&& t : threads)
                        {
                            if (t.second.action == thread_info::none)
                                t.second.set_action(c, begin, end);
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
                    if (interrupt_count == 1)
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
                reg(str, regn, query_thread_id);
                send_packet(str);
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
                    reg(str, i, query_thread_id);
                send_packet(str);
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
            encode(str, addr, len);
            send_packet(str);
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
                    if (debugmsg and t.eip() != jmp)
                        fmt::print(stderr, "JUMP to {:#x}\n", jmp);
                    t.jmp(jmp);
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
                    if (debugmsg and t.eip() != jmp)
                        fmt::print(stderr, "JUMP to {:#x}\n", jmp);
                    t.jmp(jmp);
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
            if (debugmsg) fmt::print(stdout, "KILL signal received.");
            for (auto&& t : threads) t.second.set_action('c');
            redirect_exception(current_exception, dpmi::detail::kill);
            it = fmt::format_to(it, "X{:0>2x}", posix_signal(current_thread->last_stop_signal));
            send_packet(str);
            uninstall_gdb_interface();
        }
        else send_packet("");   // unknown packet
    }

    [[gnu::hot]] static bool handle_exception(exception_num exc, const exception_info& info)
    {
        auto* const r = info.registers;
        auto* const f = info.frame;
        if (debugmsg) fmt::print(stderr, "entering exception 0x{:0>2x} from {:#x}\n",
                                 std::uint8_t { exc }, std::uintptr_t { f->fault_address.offset });
        if (not debug_mode)
        {
            if (debugmsg) fmt::print(stderr, "already killed!\n");
            return false;
        }

        if (exc == 0x03) f->fault_address.offset -= 1;

        auto catch_exception = []
        {
            fmt::print(stderr, "Exception occured while communicating with GDB.\n"
                               "caused by this packet: {}\n"
                               "good={} bad={} fail={} eof={}\n",
                       raw_packet_string,
                       gdb->good(), gdb->bad(), gdb->fail(), gdb->eof());
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

            if (debugmsg) fmt::print(stderr, "leaving exception 0x{:0>2x}, resuming at {:#x}\n",
                                     std::uint8_t { exc }, std::uintptr_t { f->fault_address.offset });
        };

        auto clear_trap_signals = []
        {
            for (auto i = current_thread->signals.begin(); i != current_thread->signals.end();)
            {
                if (is_trap_signal(*i)) i = current_thread->signals.erase(i);
                else ++i;
            }
        };

        if (f->fault_address.segment != main_cs and f->fault_address.segment != ring0_cs) [[unlikely]]
        {
            if (exc == exception_num::trap) return true; // keep stepping until we get back to our own code
            fmt::print(stderr, "Can't debug this!  CS is neither 0x{:0>4x} nor 0x{:0>4x}.\n"
                               "{}\n",
                        main_cs, ring0_cs,
                        cpu_category { }.message(info.num));
            info.frame->print();
            info.registers->print();
            return false;
        }

        if (debugger_reentry) [[unlikely]]
        {
            if (exc == 0x01 or exc == 0x03)
            {   // breakpoint in debugger code, ignore
                if (debugmsg) fmt::print(stderr, "reentry caused by breakpoint, ignoring.\n");
                leave();
                f->flags.trap = false;
                return true;
            }
            if (debugmsg)
            {
                fmt::print(stderr, "debugger re-entry!\n");
                static_cast<dpmi10_exception_frame*>(f)->print();
                r->print();
            }
            throw_cpu_exception(info);
        }

        try
        {
            debugger_reentry = true;
            populate_thread_list();
            if (packet_available()) current_thread->signals.insert(packet_received);

            if (exc == exception_num::breakpoint and current_signal != -1)
            {
                if (debugmsg) fmt::print(stderr, "break with signal 0x{:0>2x}\n", int { current_signal });
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
                    if (debugmsg) fmt::print(stderr, "trap masked at {:#x}\n",
                                             std::uintptr_t { f->fault_address.offset });
                }
                else
                {
                    current_thread->trap_mask = 0;     // serious fault occured, undo trap masks
                }
            }

            if (debugmsg)
            {
                static_cast<dpmi10_exception_frame*>(f)->print();
                r->print();
            }
            current_exception = info;

            for (auto&&t : threads)
            {
                if (t.second.action == thread_info::none)
                {
                    if (thread_events_enabled) t.second.set_action('t');
                    else t.second.set_action('c');

                    if (t.second.thread->get_state() == jw::detail::thread::starting)
                        t.second.signals.insert(thread_started);
                }

                if (current_thread->signals.count(thread_switched) and t.second.action == thread_info::stop)
                    t.second.signals.insert(thread_suspended);
            }

            current_thread->signals.erase(thread_switched);

            if (exc == exception_num::trap and current_thread->signals.count(watchpoint_hit) == 0 and
                current_thread->action == thread_info::step_range and
                current_exception.frame->fault_address.offset >= current_thread->step_range_begin and
                current_exception.frame->fault_address.offset <= current_thread->step_range_end)
            {
                if (debugmsg) fmt::print(stderr,"range step until {:#x}, now at {:#x}\n",
                                         current_thread->step_range_end, std::uintptr_t { f->fault_address.offset });
                clear_trap_signals();
            }

            if (debugmsg) fmt::print(stderr, "signals: {}\n",
                                     fmt::join(current_thread->signals, ", "));

            stop_reply();

            if (config::enable_gdb_interrupts and current_exception.frame->flags.interrupts_enabled) asm("sti");

            auto cant_continue = []
            {
                for (auto&& t : threads)
                    if (t.second.thread->active() and
                        t.second.action == thread_info::none) return true;
                return false;
            };
            do
            {
                try
                {
                    handle_packet();
                }
                catch (...)
                {
                    // TODO: determine action based on last packet / signal
                    if (not replied) send_packet("E04"); // last command caused another exception (most likely page fault after a request to read memory)
                }
                recv_ack();
            } while (cant_continue());

            while (sent_packets.size() > 0 and debug_mode) recv_ack();
        }
        catch (...) { print_exception(); catch_exception(); }
        asm("cli");

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

        gdb.emplace(cfg);
        gdb->exceptions(std::ios::failbit | std::ios::badbit | std::ios::eofbit);

        irq_signal.emplace([](const exception_info& e)
        {
            current_signal = packet_received;
            handle_exception(exception_num::breakpoint, e);
        });

        serial_irq = std::make_unique<irq_handler>([]
        {
            if (debugger_reentry) return;
            if (not packet_available()) return;
            irq_signal->raise();
        });

        for (auto&& s : { SIGHUP, SIGABRT, SIGTERM, SIGKILL, SIGQUIT, SIGILL, SIGINT })
            signal_handlers[s] = std::signal(s, csignal);

        auto install_exception_handler = [](auto&& e) { exception_handlers[e] = std::make_unique<exception_handler>(e, [e](const auto& i) { return handle_exception(e, i); }); };

        for (auto e = 0x00; e <= 0x0e; ++e)
            install_exception_handler(e);

        serial_irq->set_irq(cfg.irq);
        serial_irq->enable();

        for (auto&& e : { 0x10, 0x11, 0x12, 0x13, 0x14, 0x1e })
        {
            try { install_exception_handler(e); }
            catch (const dpmi_error&) { /* ignore */ }
        }
    }

    static void uninstall_gdb_interface()
    {
        debug_mode = false;
        serial_irq.reset();
        watchpoints.clear();
        for (auto&& bp : breakpoints) *reinterpret_cast<std::byte*>(bp.first) = bp.second;
        for (auto&& e : exception_handlers) e.reset();
        for (auto&& s : signal_handlers) std::signal(s.first, s.second);
    }

    void notify_gdb_exit(byte result)
    {
        string str { &memres };
        fmt::format_to(std::back_inserter(str), "W{:0>2x}", result);
        send_packet(str);
        uninstall_gdb_interface();
    }
}

namespace jw::debug
{
    trap_mask::trap_mask() noexcept // TODO: ideally this should treat interrupts as separate 'threads'
    {
        if (not debug()) { failed = true; return; }
        if (detail::debugger_reentry) { failed = true; return; }
        ++detail::threads[jw::detail::scheduler::current_thread_id()].trap_mask;
    }

    trap_mask::~trap_mask() noexcept
    {
        force_frame_pointer();
        if (failed) return;
        auto& t = detail::threads[jw::detail::scheduler::current_thread_id()];
        t.trap_mask = std::max(t.trap_mask - 1, 0l);
        if (t.trap_mask == 0 and [&t]
        {
            for (auto&& s : t.signals) if (detail::is_trap_signal(s)) return true;
            return false;
        }()) break_with_signal(detail::trap_unmasked);
    }
}
#endif

namespace jw::debug
{
    _Unwind_Reason_Code unwind_print_trace(_Unwind_Context* c, void*)
    {
        fmt::print(stderr, " --> {: >11x}", _Unwind_GetIP(c));
        return _URC_NO_REASON;
    }

    void print_backtrace() noexcept
    {
        fmt::print(stderr, "Backtrace  ");
        _Unwind_Backtrace(unwind_print_trace, nullptr);
        fmt::print(stderr, "\n");
    }
}
