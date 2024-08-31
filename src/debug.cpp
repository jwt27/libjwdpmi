/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2017 - 2024 J.W. Jagersma, see COPYING.txt for details    */

#include <array>
#include <cstring>
#include <memory>
#include <csignal>
#include <cstdlib>
#include <string_view>
#include <set>
#include <ranges>
#include <unwind.h>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>
#include <jw/main.h>
#include <jw/dpmi/fpu.h>
#include <jw/dpmi/dpmi.h>
#include <jw/debug.h>
#include <jw/dpmi/cpu_exception.h>
#include <jw/dpmi/detail/selectors.h>
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
    static int posix_signal(int exc) noexcept;
    static bool is_fault_signal(int) noexcept;
    static void uninstall_gdb_interface();

    const bool debugmsg = config::enable_gdb_debug_messages;

    bool debug_mode { false };
    int current_signal { -1 };
    static bool thread_events_enabled { false };
    static exception_info current_exception;

    using resource_type = locked_pool_resource;

    template<typename T = std::byte>
    using allocator = default_constructing_allocator_adaptor<monomorphic_allocator<resource_type, T>>;

    using string = std::basic_string<char, std::char_traits<char>, allocator<char>>;

    struct packet_string : public std::string_view
    {
        char delim;
        template <typename T, typename U>
        packet_string(T&& str, U&& delimiter): std::string_view(std::forward<T>(str)), delim(std::forward<U>(delimiter)) { }
        using std::string_view::operator=;
        using std::string_view::basic_string_view;
    };

    static constexpr std::size_t max_watchpoints { 8 };

    static constexpr std::size_t bufsize { 4096 };
    static char txbuf[bufsize];
    static std::size_t tx_size { 0 };

    using scheduler = jw::detail::scheduler;
    using thread = jw::detail::thread;
    using thread_id = jw::detail::thread_id;
    static constexpr thread_id main_thread_id = jw::detail::thread::main_thread_id;
    static constexpr thread_id all_threads_id { 0 };

    struct thread_info;

    static thread_info* get_info(thread* t) noexcept
    {
        return static_cast<thread_info*>(t->debug_info);
    }

    static thread* get_thread(thread_id id) noexcept
    {
        auto* const t = scheduler::get_thread(id);
        if (t and not get_info(t))
            return nullptr;

        return t;
    }

    static thread* current_thread() noexcept
    {
        return scheduler::current_thread();
    }

    static auto all_threads()
    {
        return scheduler::all_threads()
            | std::views::transform([](const thread& t) { return const_cast<thread*>(&t); })
            | std::views::filter([](thread* t) { return get_info(t) != nullptr; });
    }

    struct watchpoint : debug::watchpoint
    {
        watchpoint(std::uintptr_t near_addr, std::size_t bytes, watchpoint_type t)
            : debug::watchpoint { dpmi::near_to_linear(near_addr), bytes, t }
            , address { near_addr }
            , size { static_cast<std::uint8_t>(bytes) }
            , type { t }
        { }

        const std::uintptr_t address;
        const std::uint8_t size;
        const watchpoint_type type;
    };

    static std::optional<watchpoint::watchpoint_type> watchpoint_type(char z)
    {
        switch (z)
        {
        case '1': return { watchpoint::execute };
        case '2': return { watchpoint::write };
        case '3': return { watchpoint::read_write };
        case '4': return { watchpoint::read_write };
        }
        return std::nullopt;
    }

    struct thread_info
    {
        std::bitset<max_watchpoints> watchpoints { };
        int signal { -1 };
        int last_stop_signal { -1 };
        std::uintptr_t step_range_begin { 0 };
        std::uintptr_t step_range_end { 0 };
        std::int32_t trap_mask { 0 };

        enum
        {
            stop,
            cont,
            step,
            cont_sig,
            step_sig,
        } action;

        bool stepping() const
        {
            switch (action)
            {
            case cont:
            case cont_sig:
                return false;

            case step:
            case step_sig:
            case stop:
                return true;

            default:
                throw std::exception();
            }
        }

        bool do_action()
        {
            switch (action)
            {
            case step_sig:
                action = step;
                return false;

            case cont_sig:
                action = cont;
                return false;

            case step:
            case cont:
            case stop:
                return true;

            default:
                throw std::exception();
            }
        }
    };

    static void set_action(thread* t, char a, std::uintptr_t rbegin = 0, std::uintptr_t rend = 0)
    {
        auto* const ti = get_info(t);
        ti->step_range_begin = 0;
        ti->step_range_end = 0;
        switch (a)
        {
        case 'c':
            ti->action = thread_info::cont;
            break;

        case 's':
            ti->action = thread_info::step;
            break;

        case 'C':
            if (not is_fault_signal(ti->last_stop_signal))
                ti->action = thread_info::cont;
            else
                ti->action = thread_info::cont_sig;
            break;

        case 'S':
            if (not is_fault_signal(ti->last_stop_signal))
                ti->action = thread_info::step;
            else
                ti->action = thread_info::step_sig;
            break;

        case 'r':
            ti->step_range_begin = rbegin;
            ti->step_range_end = rend;
            ti->action = thread_info::step;
            break;

        case 't':
            // ignore
            break;
        }
    }

    // Register order and sizes found in {gdb source}/gdb/regformats/i386/i386.dat
    enum regnum : std::uint8_t
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

    constexpr std::array<std::uint8_t, 41> regsize
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

    static int posix_signal(int exc) noexcept
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
        case SIGNOFP: return sigemt;
        case SIGTRAP: return sigtrap;

            // other signals
        case continued:
            return sigcont;

        case -1:
        case packet_received:
            return 0;

        case thread_finished:
        case thread_started:
            return sigstop;

        default:
            return sigusr1;
        }
    }

    static bool is_fault_signal(int exc) noexcept
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

    static bool is_stop_signal(int exc) noexcept
    {
        switch (exc)
        {
        default:
            return true;

        case packet_received:
        case -1:
            return false;
        }
    }

    static bool is_trap_signal(int exc) noexcept
    {
        switch (exc)
        {
        default:
            return false;

        case exception_num::trap:
        case exception_num::breakpoint:
        case continued:
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
        case thread_finished:
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

    // Decode big-endian hex string
    static auto decode(std::string_view in)
    {
        static constexpr auto table = []
        {
            constexpr std::string_view hex { "0123456789abcdef" };
            std::array<std::uint8_t, 128> tbl;
            tbl.fill(0xff);
            for (unsigned i = 0; i != hex.size(); ++i)
                tbl[hex[i]] = i;
            return tbl;
        }();

        std::uint32_t result { };
        if (in[0] == '-') return all_threads_id;
        for (const std::uint8_t c : in)
        {
            if (c > table.size())
                goto fail;
            const auto nib = table[c];
            if (nib == 0xff)
                goto fail;

            result <<= 4;
            result |= nib;
        }
        return result;

    fail:
        throw std::invalid_argument { "decode() failed: "s + in.data() };
    }

    // Decode little-endian hex string
    template <typename T>
    static bool reverse_decode(std::string_view in, T* out, std::size_t len = sizeof(T))
    {
        len = std::min(len, in.size() / 2);
        auto ptr = reinterpret_cast<std::uint8_t*>(out);
        for (std::size_t i = 0; i < len; ++i)
        {
            ptr[i] = decode(in.substr(i * 2, 2));
        }
        return true;
    }

    static char* new_tx() noexcept
    {
        return txbuf + 1;
    }

    [[nodiscard]]
    static char* append(char* p, std::string_view str)
    {
        return std::copy(str.begin(), str.end(), p);
    }

    // Encode little-endian hex string
    template <typename T>
    [[nodiscard]]
    static char* encode(char* out, const T* in, std::size_t len = sizeof(T))
    {
        static constexpr char hex[] = "0123456789abcdef";
        auto* const ptr = reinterpret_cast<const volatile std::uint8_t*>(in);

        for (std::size_t i = 0; i < len; ++i)
        {
            const auto b = ptr[i];
            const auto hi = b >> 4;
            const auto lo = b & 0xf;
            *out++ = hex[hi];
            *out++ = hex[lo];
        }
        return out;
    }

    [[nodiscard]]
    static char* encode_null(char* p, std::size_t len)
    {
        return std::fill_n(p, len * 2, 'x');
    }

    template<typename T>
    [[nodiscard]]
    static char* fpu_reg(char* out, regnum reg, const T* fpu)
    {
        assume(reg >= st0);
        switch (reg)
        {
        case st0: case st1: case st2: case st3: case st4: case st5: case st6: case st7:
            return encode(out, &fpu->st[reg - st0], regsize[reg]);
        case fctrl: { std::uint32_t s = fpu->fctrl; return encode(out, &s); }
        case fstat: { std::uint32_t s = fpu->fstat; return encode(out, &s); }
        case ftag:  { std::uint32_t s = fpu->ftag;  return encode(out, &s); }
        case fiseg: { std::uint32_t s = fpu->fiseg; return encode(out, &s); }
        case fioff: { std::uint32_t s = fpu->fioff; return encode(out, &s); }
        case foseg: { std::uint32_t s = fpu->foseg; return encode(out, &s); }
        case fooff: { std::uint32_t s = fpu->fooff; return encode(out, &s); }
        case fop:   { std::uint32_t s = fpu->fop;   return encode(out, &s); }
        default:
            if constexpr (std::is_same_v<T, fxsave_data>)
            {
                if (reg == mxcsr) return encode(out, &fpu->mxcsr);
                else return encode(out, &fpu->xmm[reg - xmm0]);
            }
            else return encode_null(out, regsize[reg]);
        }
    }

    [[nodiscard]]
    static char* reg(char* out, regnum reg, thread* t)
    {
        if (reg > reg_max)
            throw std::out_of_range { "invalid register" };

        if (t == current_thread())
        {
            const auto* const r = current_exception.registers;
            const auto* const f = current_exception.frame;
            const auto* const d10f = static_cast<const dpmi10_exception_frame*>(current_exception.frame);
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
            if (not t)
                return encode_null(out, regsize[reg]);

            const auto* const r = t->get_context();
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
                if (reg == mxcsr)
                    return reverse_decode(value, &fpu->mxcsr, regsize[reg]);
                else
                    return reverse_decode(value, &fpu->xmm[reg - xmm0], regsize[reg]);
            }
            else return false;
        }
    }

    static bool set_reg(regnum reg, const std::string_view& value, thread* t)
    {
        if (t == current_thread())
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
            case ds: if (dpmi10_frame) { return reverse_decode(value.substr(0, 4), &d10f->ds, 2); }
                     else return false;
            case es: if (dpmi10_frame) { return reverse_decode(value.substr(0, 4), &d10f->es, 2); }
                     else return false;
            case fs: if (dpmi10_frame) { return reverse_decode(value.substr(0, 4), &d10f->fs, 2); }
                     else return false;
            case gs: if (dpmi10_frame) { return reverse_decode(value.substr(0, 4), &d10f->gs, 2); }
                     else return false;
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
            if (not t)
                return false;

            auto* const r = t->get_context();
            if (debugmsg) fmt::print(stderr, "set thread {:d} register {}={}\n", t->id, regname[reg], value);
            switch (reg)
            {
            case ebx:    return reverse_decode(value, &r->ebx, regsize[reg]);
            case ebp:    return reverse_decode(value, &r->ebp, regsize[reg]);
            case esi:    return reverse_decode(value, &r->esi, regsize[reg]);
            case edi:    return reverse_decode(value, &r->edi, regsize[reg]);
            case fs:     return reverse_decode(value.substr(0, 4), &r->fs, 2);
            case gs:     return reverse_decode(value.substr(0, 4), &r->gs, 2);
            default: return false;
            }
        }
    }

    static std::uint8_t checksum(std::string_view s)
    {
        std::uint8_t r { 0 };
        for (std::uint8_t c : s)
            r += c;
        return r;
    }

    static void kill()
    {
        uninstall_gdb_interface();
        terminate();
    }

    struct gdbstub
    {
        resource_type memres { 1_MB };
        string raw_packet_string { &memres };
        std::deque<packet_string, allocator<packet_string>> packet { &memres };
        bool received { false };
        bool replied { false };
        bool killed { false };
        std::atomic_flag reentry { false };
        thread* query_thread { nullptr };
        std::array<std::optional<watchpoint>, max_watchpoints> watchpoints;
        std::pmr::map<std::uintptr_t, std::byte> breakpoints { &memres };
        std::map<int, void(*)(int)> signal_handlers { };
        std::pmr::map<std::pmr::string, std::pmr::string> supported { &memres };
        io::rs232_stream com;
        std::array<std::optional<exception_handler>, 0x20> exception_handlers;

        dpmi::async_signal irq_signal { [this](const exception_info& e)
        {
            int signal = current_signal;
            current_signal = packet_received;
            e.frame->fault_address.offset += 1;
            handle_exception(exception_num::breakpoint, e);
            current_signal = signal;
        } };

        dpmi::irq_handler serial_irq { [this]
        {
            if (reentry.test()) return;
            if (not packet_available()) return;
            irq_signal.raise();
        } };

        gdbstub(const io::rs232_config& cfg)
            : com { cfg }
        {
            com.exceptions(std::ios::failbit | std::ios::badbit | std::ios::eofbit);

            auto install_exception_handler = [&](auto e)
            {
                exception_handlers[e].emplace(e, [e, this](const auto& info)
                {
                    return handle_exception(e, info);
                });
            };

            for (auto e = 0x00; e <= 0x0e; ++e)
                install_exception_handler(e);

            serial_irq.set_irq(cfg.irq);
            serial_irq.enable();

            for (auto&& e : { 0x10, 0x11, 0x12, 0x13, 0x14, 0x1e })
            {
                try { install_exception_handler(e); }
                catch (const dpmi_error&) { /* ignore */ }
            }
        }

        ~gdbstub()
        {
            for (const auto& s : signal_handlers)
                std::signal(s.first, s.second);
            for (const auto& bp : breakpoints)
                *reinterpret_cast<std::byte*>(bp.first) = bp.second;
        }

        void set_breakpoint(std::uintptr_t);
        bool clear_breakpoint(std::uintptr_t);
        bool disable_breakpoint(std::uintptr_t);
        void enable_all_breakpoints();

        void send(std::string_view);
        void send_txbuf(const char* end);
        bool receive();
        bool packet_available();

        void stop_reply(bool force = false);
        void handle_packet();
        bool handle_exception(exception_num, const exception_info&);
    };

    static constinit gdbstub* gdb { nullptr };

    inline void gdbstub::set_breakpoint(std::uintptr_t at)
    {
        auto* ptr = reinterpret_cast<std::byte*>(at);
        breakpoints.try_emplace(at, *ptr);
        *ptr = std::byte { 0xcc };
    }

    inline bool gdbstub::clear_breakpoint(std::uintptr_t at)
    {
        auto* const ptr = reinterpret_cast<std::byte*>(at);
        const auto i = breakpoints.find(at);
        if (i != breakpoints.end())
        {
            *ptr = i->second;
            breakpoints.erase(i);
            return true;
        }
        else return false;
    }

    inline bool gdbstub::disable_breakpoint(std::uintptr_t at)
    {
        auto* const ptr = reinterpret_cast<std::byte*>(at);
        const auto i = breakpoints.find(at);
        if (i != breakpoints.end())
        {
            *ptr = i->second;
            return true;
        }
        else return false;
    }

    inline void gdbstub::enable_all_breakpoints()
    {
        for (const auto& bp : breakpoints)
            *reinterpret_cast<std::byte*>(bp.first) = std::byte { 0xcc };
    }

    inline bool gdbstub::packet_available()
    {
        return com.rdbuf()->in_avail() != 0;
    }

    inline void gdbstub::send(std::string_view output)
    {
        if (config::enable_gdb_protocol_dump)
            fmt::print(stderr, "send --> \"{}\"\n", output);

        if (output.size() > bufsize - 4) [[unlikely]]
        {
            fmt::print(stderr, "TX packet too large: {} bytes\n", output.size());
            halt();
        }

        auto* p = txbuf;
        *p++ = '$';

        std::size_t i = 0;
        while (i < output.size())
        {
            const char ch = output[i];
            auto j = output.find_first_not_of(ch, i);
            if (j == output.npos)
                j = output.size();
            auto n = j - i;
            if (n > 3)
            {
                n = std::min<std::size_t>(n, 98);   // above 98, RLE byte would be non-printable
                if (n == 7 or n == 8) n = 6;        // RLE byte can't be '#' or '$'
                *p++ = ch;
                *p++ = '*';
                *p++ = static_cast<char>(n + 28);
            }
            else p = std::fill_n(p, n, ch);
            i += n;
        }

        const auto sum = checksum({ txbuf + 1, p });
        *p++ = '#';
        p = encode(p, &sum, 1);
        tx_size = p - txbuf;
        com.write(txbuf, tx_size);
        com.flush();
        replied = true;
    }

    inline void gdbstub::send_txbuf(const char* end)
    {
        return send({ txbuf + 1, end });
    }

    inline bool gdbstub::receive()
    {
        static constinit bool bad = false;
        char sum[2];
        raw_packet_string.clear();
        com.clear();
        com.force_flush();
        if (com.rdbuf()->in_avail() == 0)
            return false;

        try
        {
            switch (com.get())
            {
            case '-': [[unlikely]]
                fmt::print(stderr, "NACK\n");
                com.write(txbuf, tx_size);
                [[fallthrough]];

            default:
                return false;

            case 0x03:
                raw_packet_string = "vCtrlC";
                goto parse;

            case '$':
                break;
            }

            replied = false;
            received = false;
            raw_packet_string.clear();
            std::getline(com, raw_packet_string, '#');
            com.read(sum, 2);
        }
        catch (const std::exception& e)
        {
            const auto eof = com.eof();
            bad |= com.bad();
            com.clear();

            if (eof)
            {
                raw_packet_string = "vCtrlC";
                goto parse;
            }

            fmt::print(stderr, "Error while receiving gdb packet: {}\n", e.what());
            if (com.rdbuf()->in_avail() != -1)
            {
                fmt::print(stderr, "Received so far: \"{}\"\n", raw_packet_string);
                if (bad)
                    fmt::print(stderr, "Malformed character: \'{}\'\n", com.get());
                com.put('-');
            }
            return false;
        }

        if (decode(std::string_view { sum, 2 }) != checksum(raw_packet_string)) [[unlikely]]
        {
            fmt::print(stderr, "Bad checksum: \"{}\": {}, calculated: {:0>2x}\n",
                        raw_packet_string, sum, checksum(raw_packet_string));
            com.put('-');
            return false;
        }
        else com.put('+');

    parse:
        if (config::enable_gdb_protocol_dump)
            fmt::print(stderr, "recv <-- \"{}\"\n", raw_packet_string);
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
        received = true;
        return true;
    }

    inline void gdbstub::stop_reply(bool force)
    {
        auto* const t = current_thread();
        auto* const ti = get_info(t);
        int signal = ti->signal;
        ti->signal = -1;

        if (not is_stop_signal(signal) and force)
            signal = ti->last_stop_signal;

        if (not is_stop_signal(signal))
            return;

        ti->action = thread_info::stop;
        ti->last_stop_signal = signal;

        auto* p = new_tx();

        if (signal == thread_finished)
        {
            p = fmt::format_to(p, "w00;{:x}", t->id);
        }
        else
        {
            p = fmt::format_to(p, "T{:0>2x}", posix_signal(signal));
            p = append(p, "8:"); p = reg(p, eip, t); *p++ = ';';
            p = append(p, "4:"); p = reg(p, esp, t); *p++ = ';';
            p = append(p, "5:"); p = reg(p, ebp, t); *p++ = ';';
            p = fmt::format_to(p, "thread:{:x};", t->id);
            if (signal == thread_started)
            {
                p = append(p, "create:;"sv);
            }
            else if (is_trap_signal(signal))
            {
                if (ti->watchpoints.none())
                    p = append(p, "swbreak:;");
                else for (unsigned i = 0; i != max_watchpoints; ++i)
                {
                    if (not ti->watchpoints[i])
                        continue;

                    if (watchpoints[i]->type == watchpoint::execute)
                        p = append(p, "hwbreak:;");
                    else
                        p = fmt::format_to(p, "watch:{:x};", watchpoints[i]->address);
                }
            }
        }

        send_txbuf(p);
        query_thread = t;
    }

    [[gnu::hot]] inline void gdbstub::handle_packet()
    {
        if (not received)
            return;
        received = false;

        const char p = packet.front().delim;
        if (p == '?')   // stop reason
        {
            stop_reply(true);
        }
        else if (p == 'q')  // query
        {
            auto& q = packet[0];
            if (q == "Supported"sv)
            {
                tx_size = 0;
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
                send("PacketSize=399;swbreak+;hwbreak+;QThreadEvents+"sv);
            }
            else if (q == "Attached"sv)
                send("0");
            else if (q == "C"sv)
            {
                auto* p = new_tx();
                p = fmt::format_to(p, "QC{:x}", current_thread()->id);
                send_txbuf(p);
            }
            else if (q == "fThreadInfo"sv)
            {
                auto* p = new_tx();
                *p++ = 'm';
                for (thread* t : all_threads())
                    p = fmt::format_to(p, "{:x},", t->id);
                send_txbuf(p);
            }
            else if (q == "sThreadInfo"sv)
                send("l");
            else if (q == "ThreadExtraInfo"sv)
            {
                using namespace jw::detail;
                static string msg { &memres };
                msg.clear();
                auto id = decode(packet[1]);
                if (auto* t = get_thread(id))
                {
                    fmt::format_to(std::back_inserter(msg), "{}{}: ",
                                   t->get_name(), t == current_thread() ? " (*)"sv : ""sv);
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

                auto* p = new_tx();
                p = encode(p, msg.c_str(), msg.size());
                send_txbuf(p);
            }
            else send("");
        }
        else if (p == 'Q')
        {
            auto& q = packet[0];
            if (q == "ThreadEvents"sv)
            {
                thread_events_enabled = packet[1][0] - '0';
                send("OK");
            }
            else send("");
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
                send("vCont;s;S;c;C;t;r"sv);
            }
            else if (v == "Cont"sv)
            {
                struct thread_action
                {
                    thread_id id;
                    std::uintptr_t begin, end;
                    char action;
                };
                thread_action actions[packet.size()];
                std::size_t n = 0;

                for (std::size_t i = 1; i < packet.size(); ++i)
                {
                    std::uintptr_t begin { 0 }, end { 0 };
                    char c { packet[i][0] };
                    if (c == 'r')
                    {
                        if (i + 1 >= packet.size() or packet[i + 1].delim != ',')
                        {
                            send("E00");
                            return;
                        }
                        begin = decode(packet[i].substr(1));
                        end = decode(packet[i + 1]);
                        ++i;
                    }
                    if (i + 1 < packet.size() and packet[i + 1].delim == ':')
                    {
                        auto id = decode(packet[i + 1]);
                        if (not get_thread(id))
                        {
                            send("E00");
                            return;
                        }

                        ++i;
                        actions[n++] = { id, begin, end, c };
                    }
                    else
                    {
                        for (thread* t : all_threads())
                            set_action(t, c, begin, end);
                    }
                }

                for (std::size_t i = 0; i != n; ++i)
                {
                    auto& a = actions[i];
                    set_action(get_thread(a.id), a.action, a.begin, a.end);
                }
            }
            else if (v == "CtrlC")
            {
                get_info(current_thread())->signal = SIGINT;
                send("OK");
                stop_reply();
            }
            else send("");
        }
        else if (p == 'H')  // set current thread
        {
            auto id = decode(packet[0].substr(1));
            auto* t = get_thread(id);
            if (t or id == all_threads_id)
            {
                if (packet[0][0] == 'g')
                {
                    if (id == all_threads_id)
                        query_thread = current_thread();
                    else
                        query_thread = t;
                }
                send("OK");
            }
            else send("E00");
        }
        else if (p == 'T')  // is thread alive?
        {
            auto id = decode(packet[0]);
            if (get_thread(id))
                send("OK");
            else
                send("E01");
        }
        else if (p == 'p')  // read one register
        {
            if (query_thread)
            {
                auto* p = new_tx();
                auto regn = static_cast<regnum>(decode(packet[0]));
                p = reg(p, regn, query_thread);
                send_txbuf(p);
            }
            else send("E00");
        }
        else if (p == 'P')  // write one register
        {
            if (set_reg(static_cast<regnum>(decode(packet[0])), packet[1], query_thread))
                send("OK");
            else
                send("E00");
        }
        else if (p == 'g')  // read registers
        {
            if (query_thread)
            {
                auto* p = new_tx();
                for (auto i = eax; i <= gs; ++i)
                    p = reg(p, i, query_thread);
                send_txbuf(p);
            }
            else send("E00");
        }
        else if (p == 'G')  // write registers
        {
            regnum reg { };
            std::size_t pos { };
            bool ok { true };
            while (ok and pos < packet[0].size())
            {
                ok &= set_reg(reg, packet[0].substr(pos), query_thread);
                pos += regsize[reg] * 2;
                ++reg;
            }
            if (ok)
                send("OK");
            else
                send("E00");
        }
        else if (p == 'm')  // read memory
        {
            auto* addr = reinterpret_cast<byte*>(decode(packet[0]));
            std::size_t len = decode(packet[1]);

            auto* p = new_tx();
            p = encode(p, addr, len);
            send_txbuf(p);
        }
        else if (p == 'M')  // write memory
        {
            auto* addr = reinterpret_cast<byte*>(decode(packet[0]));
            std::size_t len = decode(packet[1]);
            if (reverse_decode(packet[2], addr, len))
                send("OK");
            else
                send("E00");
        }
        else if (p == 'Z')  // set break/watchpoint
        {
            auto& z = packet[0][0];
            std::uintptr_t addr = decode(packet[1]);
            if (z == '0')   // set breakpoint
            {
                if (packet.size() > 3)  // conditional breakpoint
                {
                    send("");    // not implemented (TODO)
                    return;
                }
                set_breakpoint(addr);
                send("OK");
            }
            else            // set watchpoint
            {
                try
                {
                    const auto type = watchpoint_type(z);
                    if (not type)
                    {
                        send("");
                        return;
                    }
                    std::size_t size = decode(packet[2]);
                    bool ok = false;
                    for (auto& wp : watchpoints)
                    {
                        if (wp)
                            continue;

                        wp.emplace(addr, size, *type);
                        ok = true;
                        break;
                    }
                    if (ok)
                        send("OK");
                    else
                        throw std::runtime_error { "this should never happen" };
                }
                catch (...)
                {
                    send("E00");
                }
            }
        }
        else if (p == 'z')  // remove break/watchpoint
        {
            auto& z = packet[0][0];
            std::uintptr_t addr = decode(packet[1]);
            if (z == '0')   // remove breakpoint
            {
                if (clear_breakpoint(addr))
                    send("OK");
                else
                    send("E00");
            }
            else            // remove watchpoint
            {
                const auto type = watchpoint_type(z);
                if (not type)
                {
                    send("");
                    return;
                }

                unsigned n = 0;
                for (auto& wp : watchpoints)
                {
                    if (not wp)
                        continue;
                    if (wp->address != addr)
                        continue;
                    if (wp->type != *type)
                        continue;

                    wp.reset();
                    ++n;
                }

                if (n > 0)
                    send("OK");
                else
                    send("E00");
            }
        }
        else if (p == 'k')  // kill
        {
            if (debugmsg) fmt::print(stdout, "KILL signal received.");
            for (thread* t : all_threads())
                set_action(t, 'c');
            if (redirect_exception(current_exception, kill))
            {
                auto* p = new_tx();
                p = fmt::format_to(p, "X{:0>2x}", posix_signal(get_info(current_thread())->last_stop_signal));
                send_txbuf(p);
            }
            else send("E00");
        }
        else send("");   // unknown packet
    }

    [[gnu::hot]] inline bool gdbstub::handle_exception(exception_num exc, const exception_info& info)
    {
        auto* const r = info.registers;
        auto* const f = info.frame;
        if (debugmsg)
            fmt::print(stderr, "entering exception 0x{:0>2x} from {:#x}\n",
                       std::uint8_t { exc }, std::uintptr_t { f->fault_address.offset });

        if (exc == exception_num::breakpoint)
            f->fault_address.offset -= 1;

        auto leave = [&]
        {
            enable_all_breakpoints();
            if (*reinterpret_cast<const std::uint8_t*>(f->fault_address.offset) == 0xcc)
            {
                // Don't resume on a breakpoint.
                if (disable_breakpoint(f->fault_address.offset))
                    f->flags.trap = true;           // trap on next instruction to re-enable
                else
                    f->fault_address.offset += 1;   // hardcoded breakpoint, safe to skip
            }
            f->flags.resume = true;

            if (debugmsg)
                fmt::print(stderr, "leaving exception 0x{:0>2x}, resuming at {:#x}\n",
                           std::uint8_t { exc }, std::uintptr_t { f->fault_address.offset });
        };

        if (f->fault_address.segment != main_cs and f->fault_address.segment != ring0_cs) [[unlikely]]
        {
            if (exc == exception_num::trap)
                return true; // keep stepping until we get back to our own code

            fmt::print(stderr, "Can't debug this!  CS is neither 0x{:0>4x} nor 0x{:0>4x}.\n"
                               "{}\n",
                       main_cs, ring0_cs,
                       cpu_category { }.message(info.num));
            info.frame->print();
            info.registers->print();
            return false;
        }

        local_destructor fix_popf { [&]
        {
            auto* const eip = reinterpret_cast<const std::uint8_t*>(info.frame->fault_address.offset);
            auto* const esp = reinterpret_cast<cpu_flags*>(info.frame->stack.offset);

            if (*eip == 0x9d) // POPF
                esp->trap = f->flags.trap;
        } };

        if (reentry.test_and_set()) [[unlikely]]
        {
            if ((exc == exception_num::trap) | (exc == exception_num::breakpoint))
            {   // breakpoint in debugger code, ignore
                if (debugmsg)
                    fmt::print(stderr, "re-entry caused by breakpoint, ignoring.\n");
                leave();
                current_signal = -1;
                f->flags.trap = false;
                return true;
            }
            if (debugmsg)
            {
                fmt::print(stderr, "debugger re-entry!\n");
                static_cast<const dpmi10_exception_frame*>(f)->print();
                r->print();
            }
            throw_cpu_exception(info);
        }

        auto* const ti = get_info(current_thread());
        current_exception = info;

        try
        {
            int signal = current_signal;

            if ((exc != exception_num::breakpoint) | (signal == -1))
            {
                signal = exc;
                current_signal = -1;
            }
            else if (debugmsg)
                fmt::print(stderr, "break with signal 0x{:0>2x}\n", signal);

            if (not ti) [[unlikely]]
            {
                if (is_benign_signal(signal))
                {
                    leave();
                    return true;
                }
                else [[unlikely]]
                {
                    auto* const t = current_thread();
                    fmt::print(stderr, "While entering/leaving thread {} ({}): {}\n",
                               t->id, t->get_name(), cpu_category { }.message(info.num));
                    info.frame->print();
                    info.registers->print();
                    halt();
                }
            }

            switch (signal)
            {
            case packet_received:
                if (ti->signal != -1)
                    signal = ti->signal;
                break;

            case exception_num::trap:
                for (unsigned i = 0; i != max_watchpoints; ++i)
                    if (watchpoints[i] and watchpoints[i]->triggered())
                        watchpoints[i]->reset(), ti->watchpoints[i] = true;

                if (ti->watchpoints.none() & not ti->stepping())
                {
                    // Possible reasons to be here:
                    // * To enable a previously-disabled breakpoint.
                    // * From a POPF, after stepping over PUSHF.
                    signal = -1;
                    break;
                }

                [[fallthrough]];
            case continued:
                if (ti->watchpoints.none()
                    & (f->fault_address.offset >= ti->step_range_begin)
                    & (f->fault_address.offset <= ti->step_range_end))
                {
                    if (debugmsg)
                        fmt::print(stderr, "range step until {:#x}, now at {:#x}\n",
                                   ti->step_range_end, std::uintptr_t { f->fault_address.offset });
                    signal = -1;
                }
                break;
            }
            ti->signal = signal;

            if (ti->trap_mask > 0 and is_benign_signal(ti->signal))
            {
                if (debugmsg)
                    fmt::print(stderr, "trap masked at {:#x}\n",
                                std::uintptr_t { f->fault_address.offset });

                leave();
                f->flags.trap = false;
                reentry.clear();
                return true;
            }
            ti->trap_mask = 0;

            if (ti->signal == -1)
                goto done;

            if (debugmsg)
            {
                static_cast<const dpmi10_exception_frame*>(f)->print();
                r->print();
            }

            if (config::enable_gdb_interrupts and f->flags.interrupts_enabled)
                asm("sti");

            stop_reply();

            do
            {
                receive();
                try
                {
                    handle_packet();
                }
                catch (...)
                {
                    // last command caused another exception (most likely page
                    // fault after a request to read memory)
                    // TODO: determine action based on last packet / signal
                    if (not replied)
                        send("E04");
                    else
                        print_exception();
                }
            } while (ti->action == thread_info::stop);

            com.flush();
        }
        catch (...)
        {
            fmt::print(stderr, "Exception occured while communicating with GDB.\n"
                       "last received packet: \"{}\"\n",
                       raw_packet_string);
            print_exception();
            halt();
        }
        asm ("cli");

    done:
        ti->watchpoints.reset();
        f->flags.trap = ti->stepping();
        leave();
        reentry.clear();

        return ti->do_action();
    }

    void create_thread(thread* t)
    {
        if (not debug())
            return;

        trap_mask no_step;
        t->debug_info = new (locked) thread_info { };
        set_action(t, 'c');

        if (thread_events_enabled)
            break_with_signal(debug_signals::thread_started);
    }

    void destroy_thread(thread* t)
    {
        if (thread_events_enabled)
            break_with_signal(debug_signals::thread_finished);

        trap_mask no_step;

        if (gdb and gdb->query_thread == t)
            gdb->query_thread = nullptr;

        auto* info = detail::get_info(t);
        if (info)
        {
            delete info;
            t->debug_info = nullptr;
        }
    }

    extern "C" void csignal(int signal)
    {
        break_with_signal(signal);
        if (gdb)
            gdb->signal_handlers[signal](signal);
    }

    void setup_gdb_interface(io::rs232_config cfg)
    {
        if (gdb)
            return;

        for (auto& thr : scheduler::all_threads())
        {
            auto* const t = const_cast<thread*>(&thr);
            auto* const ti = new (locked) thread_info { };
            ti->action = thread_info::cont;
            t->debug_info = ti;
        }

        gdb = new (locked) gdbstub { cfg };

        for (int s : { SIGHUP, SIGABRT, SIGTERM, SIGKILL, SIGQUIT, SIGILL, SIGINT })
            gdb->signal_handlers[s] = std::signal(s, csignal);

        debug_mode = true;
    }

    static void uninstall_gdb_interface()
    {
        debug_mode = false;
        if (gdb)
            delete gdb;
        gdb = nullptr;
    }

    void notify_gdb_exit(byte result)
    {
        if (not gdb)
            return;
        auto* p = new_tx();
        p = fmt::format_to(p, "W{:0>2x}", result);
        gdb->send_txbuf(p);
        uninstall_gdb_interface();
    }
}

namespace jw::debug
{
    trap_mask::trap_mask() noexcept
    {
        if (not detail::gdb)
            return;
        if (detail::gdb->reentry.test())
            return;

        auto* const ti = detail::get_info(detail::current_thread());
        if (not ti)
            return;

        ++ti->trap_mask;
        failed = false;
    }

    trap_mask::~trap_mask() noexcept
    {
        if (failed)
            return;

        auto* const ti = detail::get_info(detail::current_thread());
        if (not ti)
            return;

        force_frame_pointer();

        const auto n = ti->trap_mask - 1;
        if (n <= 0)
        {
            ti->trap_mask = 0;
            if ((ti->signal != -1) | (ti->stepping()))
            {
                // Resume with SIGCONT, otherwise gdb will get confused.
                break_with_signal(detail::continued);
            }
        }
        else ti->trap_mask = n;
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
