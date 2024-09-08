/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2017 - 2024 J.W. Jagersma, see COPYING.txt for details    */

#include <array>
#include <cstring>
#include <memory>
#include <csignal>
#include <cstdlib>
#include <string_view>
#include <ranges>
#include <unwind.h>
#include <fmt/format.h>
#include <jw/main.h>
#include <jw/dpmi/fpu.h>
#include <jw/dpmi/dpmi.h>
#include <jw/debug.h>
#include <jw/dpmi/cpu_exception.h>
#include <jw/dpmi/detail/selectors.h>
#include <jw/io/rs232.h>
#include <jw/alloc.h>
#include <jw/dpmi/ring0.h>
#include <jw/thread.h>
#include <jw/dpmi/async_signal.h>
#include "jwdpmi_config.h"

using namespace std::literals;
using namespace jw::dpmi;
using namespace jw::dpmi::detail;

#ifndef NDEBUG
namespace jw::debug::detail
{
    using scheduler = jw::detail::scheduler;
    using thread = jw::detail::thread;
    using thread_id = jw::detail::thread_id;

    static int posix_signal(int exc) noexcept;
    static bool is_fault_signal(int) noexcept;
    static void uninstall_gdb_interface();

    static constexpr bool debugmsg { config::enable_gdb_debug_messages };
    static constexpr thread_id main_thread_id { thread::main_thread_id };
    static constexpr thread_id all_threads_id { 0 };
    static constexpr std::size_t max_watchpoints { 8 };
    static constexpr std::size_t max_breakpoints { 256 };
    static constexpr std::size_t bufsize { 4096 };

    static constinit std::atomic_flag reentry { false };
    static constinit bool thread_events_enabled { false };
    static exception_info current_exception;

    static constinit std::size_t tx_size { 0 };
    static constinit std::size_t rx_size { 0 };
    static char txbuf[bufsize];
    static char rxbuf[bufsize];
    static char asciibuf[bufsize / 2];

    constinit bool debug_mode { false };
    constinit int current_signal { -1 };

    struct breakpoint_map
    {
        ~breakpoint_map()
        {
            clear();
        }

        bool insert(std::uintptr_t at)
        {
            if (n == max_breakpoints) [[unlikely]]
                return false;

            entries[n++] = { at, read(at) };
            return true;
        }

        bool erase(std::uintptr_t at)
        {
            for (unsigned i = n; i-- != 0;)
            {
                auto& e = entries[i];
                if (e.address != at)
                    continue;

                write(e.address, e.opcode);
                e = entries[--n];
                return true;
            }

            return false;
        }

        void enable_all()
        {
            for (unsigned i = 0; i != n; ++i)
                write(entries[i].address, std::byte { 0xcc });
        }

        bool enable_all_except(std::uintptr_t at)
        {
            bool found = false;
            for (unsigned i = 0; i != n; ++i)
            {
                const auto& e = entries[i];
                if (e.address == at)
                {
                    found = true;
                    write(e.address, e.opcode);
                }
                else write(e.address, std::byte { 0xcc });
            }
            return found;
        }

        void clear()
        {
            for (unsigned i = 0; i != n; ++i)
                write(entries[i].address, entries[i].opcode);
            n = 0;
        }

    private:
        static std::byte read(std::uintptr_t p) noexcept
        {
            return *reinterpret_cast<const std::byte*>(p);
        }

        static void write(std::uintptr_t p, std::byte b) noexcept
        {
            *reinterpret_cast<std::byte*>(p) = b;
        }

        struct entry
        {
            std::uintptr_t address;
            std::byte opcode;
        };

        std::unique_ptr<entry[]> entries { new entry[max_breakpoints] };
        std::size_t n { 0 };
    };

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

    struct thread_action
    {
        thread_id id;
        int signal;
        std::uintptr_t begin, end;
        char action;
    };

    struct thread_info
    {
        std::bitset<max_watchpoints> watchpoints { };
        int signal { -1 };
        std::uintptr_t step_range_begin { 0 };
        std::uintptr_t step_range_end { 0 };
        int trap_mask { 0 };
        bool stopped { false };
        bool stepping { false };
        bool ignore_signal { true };
        bool invalid_signal { false };
    };

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

    static void set_action(thread* t, const thread_action& a)
    {
        auto* const ti = get_info(t);
        ti->step_range_begin = 0;
        ti->step_range_end = 0;
        ti->ignore_signal = true;
        ti->invalid_signal = false;
        ti->stepping = false;
        ti->stopped = false;
        switch (a.action)
        {
        case 'r':
            ti->step_range_begin = a.begin;
            ti->step_range_end = a.end;
            [[fallthrough]];

        case 's':
            ti->stepping = true;
            [[fallthrough]];

        case 'c':
            break;

        case 'S':
            ti->stepping = true;
            [[fallthrough]];

        case 'C':
            ti->ignore_signal = false;
            ti->invalid_signal = posix_signal(ti->signal) != a.signal;
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
        return (exc >= 0) & (exc <= 20);
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
        return (exc == exception_num::trap)
            | (exc == exception_num::breakpoint)
            | (exc == continued);
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

    static std::string_view current_packet() noexcept
    {
        return { rxbuf, rx_size };
    }

    static bool starts_with(const char* p, std::string_view str) noexcept
    {
        const auto n4 = str.size() / 4 * 4;
        const auto n1 = str.size() % 4;
        const auto* const q = str.begin();
        union
        {
            std::uint8_t x8[4];
            std::uint32_t x32;
        } a, b;
        std::uint32_t neq = 0;

        for (std::size_t i = 0; i != n4; i += 4)
        {
            std::copy_n(p + i, 4, a.x8);
            std::copy_n(q + i, 4, b.x8);
            neq |= a.x32 xor b.x32;
        }

        a.x32 = b.x32 = 0;
        std::copy_n(p + n4, n1, a.x8);
        std::copy_n(q + n4, n1, b.x8);
        neq |= a.x32 xor b.x32;

        return neq == 0;
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

    [[nodiscard]]
    static char* encode_ascii(char* p, const char* end)
    {
        const auto n = end - asciibuf;
        return encode(p, asciibuf, n);
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
        bool received { false };
        bool replied { false };
        bool acked { true };
        thread* query_thread { nullptr };
        std::array<std::optional<watchpoint>, max_watchpoints> watchpoints;
        breakpoint_map breakpoints;
        std::bitset<sigmax> pass_signals { };
        std::map<int, void(*)(int)> signal_handlers { };
        io::rs232_stream com;
        std::array<std::optional<exception_handler>, 0x20> exception_handlers;

        dpmi::async_signal irq_signal { [this](const exception_info& e)
        {
            int signal = current_signal;
            current_signal = packet_received;
            handle_exception(exception_num::trap, e);
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

            for (auto e : { 0x10, 0x11, 0x12, 0x13, 0x14 })
            {
                try { install_exception_handler(e); }
                catch (const dpmi_error&) { /* ignore */ }
            }

            serial_irq.set_irq(cfg.irq);
            serial_irq.enable();
        }

        ~gdbstub()
        {
            for (const auto& s : signal_handlers)
                std::signal(s.first, s.second);
        }

        void send(std::string_view);
        void send_txbuf(const char*);
        template<typename... T>
        void print(fmt::format_string<T...>, T&&...);
        bool receive();
        bool packet_available();

        void stop_reply();
        void handle_packet();
        bool handle_exception(exception_num, const exception_info&);
    };

    static constinit gdbstub* gdb { nullptr };

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

        while (not acked)
            if (receive())
                throw std::runtime_error { "GDB protocol sequencing error" };

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
                if ((n == 7) | (n == 8)) n = 6;     // RLE byte can't be '#' or '$'
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
        acked = false;
    }

    inline void gdbstub::send_txbuf(const char* end)
    {
        return send({ txbuf + 1, end });
    }

    template<typename... T>
    inline void gdbstub::print(fmt::format_string<T...> str, T&&... args)
    {
        auto* a = asciibuf;
        a = fmt::format_to(a, std::move(str), std::forward<T>(args)...);

        auto* tx = new_tx();
        *tx++ = 'O';
        tx = encode_ascii(tx, a);
        send_txbuf(tx);
    }

    inline bool gdbstub::receive()
    {
        char sum[2];
        com.clear();
        com.force_flush();
        if (com.rdbuf()->in_avail() == 0)
            return false;

        auto ctrl_c = []
        {
            rx_size = append(rxbuf, "vCtrlC") - rxbuf;
        };

        try
        {
            switch (com.get())
            {
            case '-': [[unlikely]]
                fmt::print(stderr, "NACK\n");
                com.write(txbuf, tx_size);
                return false;

            case '+':
                acked = true;
                return false;

            default:
                return false;

            case 0x03:
                ctrl_c();
                goto parse;

            case '$':
                break;
            }

            replied = false;
            received = false;
            rx_size = 0;
            com.getline(rxbuf, bufsize, '#');
            rx_size = com.gcount() - 1;
            com.read(sum, 2);
        }
        catch (const std::exception& e)
        {
            const auto eof = com.eof();
            com.clear();

            if (eof)
            {
                ctrl_c();
                goto parse;
            }

            fmt::print(stderr, "While receiving gdb packet: {}\n", e.what());
            if (com.rdbuf()->in_avail() != -1)
                com.put('-');
            return false;
        }

        if (decode(std::string_view { sum, 2 }) != checksum(current_packet())) [[unlikely]]
        {
            fmt::print(stderr, "Bad checksum: \"{}\": {}, calculated: {:0>2x}\n",
                       current_packet(), sum, checksum(current_packet()));
            com.put('-');
            return false;
        }
        else com.put('+');

    parse:
        if (config::enable_gdb_protocol_dump)
            fmt::print(stderr, "recv <-- \"{}\"\n", current_packet());
        received = true;
        return true;
    }

    inline void gdbstub::stop_reply()
    {
        auto* const t = current_thread();
        auto* const ti = get_info(t);
        const int signal = ti->signal;
        const int posix = posix_signal(signal);

        if (not is_stop_signal(signal))
            return;

        if (pass_signals[posix] and not ti->stepping)
        {
            ti->ignore_signal = false;
            ti->invalid_signal = false;
            return;
        }

        ti->stopped = true;

        auto* p = new_tx();

        if (signal == thread_finished)
        {
            p = fmt::format_to(p, "w00;{:x}", t->id);
        }
        else
        {
            p = fmt::format_to(p, "T{:0>2x}", posix);
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

        auto* const t = current_thread();
        auto* const ti = get_info(t);
        const char* pkt = rxbuf;
        const char* const end = rxbuf + rx_size;
        rxbuf[rx_size] = '\0';

        auto get = [&]
        {
            return *pkt++;
        };

        auto get_n = [&](std::size_t n)
        {
            std::string_view str { pkt, pkt + n };
            pkt += n;
            if (pkt > end)
                throw std::runtime_error { "packet too short" };
            return str;
        };

        auto must_get = [&](char c)
        {
            if (*pkt++ != c)
                throw std::runtime_error { "unrecognized packet format" };
        };

        auto remaining = [&]
        {
            return std::string_view { pkt, end };
        };

        auto skip = [&](std::string_view str)
        {
            const bool eq = starts_with(pkt, str);
            pkt += str.size() & (static_cast<std::size_t>(not eq) - 1);
            return eq;
        };

        auto equal = [&](std::string_view str)
        {
            return starts_with(pkt, str) & (remaining().size() == str.size());
        };

        auto next = [&]
        {
            const char* p = pkt;
            while (true) switch (*p)
            {
            default:
                ++p;
                continue;

            case ',':
            case ';':
            case ':':
            case '=':
            case '\0':
                std::string_view str { pkt, p };
                pkt = p;
                return str;
            }
        };

        auto count = [&](char c)
        {
            return std::count(pkt, end, c);
        };

        switch (get())
        {
        case '?':   // stop reason
            if (not is_stop_signal(ti->signal))
                ti->signal = SIGINT;
            stop_reply();
            break;

        case 'q':   // query
            if (skip("Supported"))
            {
                tx_size = 0;
                breakpoints.clear();
                auto* p = new_tx();
                p = fmt::format_to(p, "PacketSize={:x};", bufsize - 4);
                p = append(p, "swbreak+;");
                p = append(p, "hwbreak+;");
                p = append(p, "QThreadEvents+;");
                p = append(p, "QPassSignals+");
                send_txbuf(p);
            }
            else if (skip("Attached"))
                send("0");
            else if (equal("C"))
            {
                auto* p = new_tx();
                p = fmt::format_to(p, "QC{:x}", current_thread()->id);
                send_txbuf(p);
            }
            else if (equal("fThreadInfo"))
            {
                auto* p = new_tx();
                *p++ = 'm';
                for (thread* t : all_threads())
                    p = fmt::format_to(p, "{:x},", t->id);
                send_txbuf(p);
            }
            else if (equal("sThreadInfo"))
                send("l");
            else if (skip("ThreadExtraInfo,"))
            {
                auto* a = asciibuf;
                const auto id = decode(remaining());
                if (auto* const t = get_thread(id))
                {
                    a = append(a, t->get_name());
                    if (t == current_thread())
                        a = append(a, " (*)");
                    a = append(a, ": ");
                    switch (t->get_state())
                    {
                    case thread::starting:  a = append(a, "Starting");  break;
                    case thread::running:   a = append(a, "Running");   break;
                    case thread::finishing: a = append(a, "Finishing"); break;
                    case thread::finished:  a = append(a, "Finished");  break;
                    }
                    if (t->is_suspended())  a = append(a, " (suspended)");
                    if (t->is_canceled())   a = append(a, " (canceled)");
                }
                else a = append(a, "invalid thread");

                auto* tx = new_tx();
                tx = encode_ascii(tx, a);
                send_txbuf(tx);
            }
            else goto unknown;
            break;

        case 'Q':   // set
            if (skip("ThreadEvents:"))
            {
                thread_events_enabled = get() == '1';
                send("OK");
            }
            else if (skip("PassSignals:"))
            {
                pass_signals.reset();
                do
                {
                    const auto i = decode(next());
                    if (i < pass_signals.size())
                        pass_signals.set(i);
                } while (get() == ';');
                send("OK");
            }
            else goto unknown;
            break;

        case 'v':
            if (skip("Cont"))
            {
                switch (get())
                {
                case '?':
                    send("vCont;s;S;c;C;r");
                    return;

                default:
                    goto unknown;

                case ';':
                    break;
                }

                thread_action actions[count(';') + 1];
                thread_action default_action { };
                std::size_t n = 0;

                while(true)
                {
                    thread_action a { };
                    switch (a.action = get())
                    {
                    case 'c':
                    case 's':
                    case 't':
                        break;

                    case 'C':
                    case 'S':
                        a.signal = decode(get_n(2));
                        break;

                    case 'r':
                        a.begin = decode(next());
                        must_get(',');
                        a.end = decode(next());
                        break;

                    default: [[unlikely]]
                        goto unknown;
                    }

                    char c = get();
                    switch (c)
                    {
                    case ':':
                        a.id = decode(next());
                        actions[n++] = a;
                        c = get();
                        break;

                    case ';':
                    case '\0':
                        a.id = all_threads_id;
                        default_action = a;
                        break;

                    default: [[unlikely]]
                        send("E00");
                        return;
                    }
                    if (c != ';')
                        break;
                };

                const auto& a = default_action;
                if (a.id == all_threads_id)
                    for (thread* t : all_threads())
                        set_action(t, a);

                for (std::size_t i = 0; i != n; ++i)
                {
                    const auto& a = actions[i];
                    set_action(get_thread(a.id), a);
                }
            }
            else if (equal("CtrlC"))
            {
                ti->signal = SIGINT;
                send("OK");
                stop_reply();
            }
            else goto unknown;
            break;

        case 'H':   // set current thread
            if (get() == 'g')
            {
                const auto id = decode(remaining());
                if (id == all_threads_id)
                    query_thread = current_thread();
                else
                    query_thread = get_thread(id);
                send("OK");
            }
            else goto unknown;
            break;

        case 'T':   // is thread alive?
            if (get_thread(decode(remaining())))
                send("OK");
            else
                send("E01");
            break;

        case 'p':   // read one register
            if (query_thread)
            {
                auto* p = new_tx();
                const auto i = static_cast<regnum>(decode(remaining()));
                p = reg(p, i, query_thread);
                send_txbuf(p);
            }
            else send("E00");
            break;

        case 'P':   // write one register
        {
            const auto i = static_cast<regnum>(decode(next()));
            must_get('=');
            if (set_reg(i, remaining(), query_thread))
                send("OK");
            else
                send("E00");
            break;
        }
        case 'g':   // read registers
        {
            if (query_thread)
            {
                auto* p = new_tx();
                for (auto i = eax; i <= gs; ++i)
                    p = reg(p, i, query_thread);
                send_txbuf(p);
            }
            else send("E00");
            break;
        }
        case 'G':   // write registers
        {
            const auto str { remaining() };
            regnum reg { };
            std::size_t pos { };
            bool ok { true };
            while (ok and pos < str.size())
            {
                ok &= set_reg(reg, str.substr(pos), query_thread);
                pos += regsize[reg] * 2;
                ++reg;
            }
            if (ok)
                send("OK");
            else
                send("E00");
            break;
        }
        case 'm':   // read memory
        {
            auto* addr = reinterpret_cast<std::byte*>(decode(next()));
            must_get(',');
            std::size_t len = decode(remaining());

            auto* p = new_tx();
            try { p = encode(p, addr, len); }
            catch (...)
            {
                send("E04");
                return;
            }
            send_txbuf(p);
            break;
        }
        case 'M':   // write memory
        {
            auto* addr = reinterpret_cast<std::byte*>(decode(next()));
            must_get(',');
            std::size_t len = decode(next());
            must_get(':');
            try { reverse_decode(remaining(), addr, len); }
            catch (...)
            {
                send("E04");
                return;
            }
            send("OK");
            break;
        }
        case 'Z':  // set break/watchpoint
        {
            const auto z = get();
            must_get(',');
            std::uintptr_t addr = decode(next());
            must_get(',');
            std::size_t size = decode(next());
            if (z == '0')   // set breakpoint
            {
                if (breakpoints.insert(addr))
                    send("OK");
                else
                    send("E.Too many breakpoints");
            }
            else try        // set watchpoint
            {
                const auto type = watchpoint_type(z);
                if (not type)
                    goto unknown;

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
            break;
        }
        case 'z':   // remove break/watchpoint
        {
            const auto z = get();
            must_get(',');
            std::uintptr_t addr = decode(next());
            must_get(',');
            std::size_t size = decode(remaining());
            if (z == '0')   // remove breakpoint
            {
                if (breakpoints.erase(addr))
                    send("OK");
                else
                    send("E.No such breakpoint");
            }
            else    // remove watchpoint
            {
                const auto type = watchpoint_type(z);
                if (not type)
                    goto unknown;

                unsigned n = 0;
                for (auto& wp : watchpoints)
                {
                    if (not wp)
                        continue;
                    if ((wp->address != addr)
                        | (wp->type != *type)
                        | (wp->size != size))
                        continue;

                    wp.reset();
                    ++n;
                }

                if (n > 0)
                    send("OK");
                else
                    send("E00");
            }
            break;
        }
        case 'k':   // kill
        {
            if (debugmsg) fmt::print(stdout, "KILL signal received.");
            for (thread* t : all_threads())
                set_action(t, { .action = 'c' });
            if (redirect_exception(current_exception, kill))
            {
                auto* p = new_tx();
                p = fmt::format_to(p, "X{:0>2x}", posix_signal(get_info(current_thread())->signal));
                send_txbuf(p);
            }
            else send("E00");
            break;
        }

        unknown:
        default:    // unknown packet
            send("");
        }
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
            // Don't resume on a breakpoint.
            if (breakpoints.enable_all_except(f->fault_address.offset))
                f->flags.trap = true;           // trap on next instruction to re-enable
            else if (*reinterpret_cast<const std::uint8_t*>(f->fault_address.offset) == 0xcc)
                f->fault_address.offset += 1;   // hardcoded breakpoint, safe to skip
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
                       main_cs, ring0_cs, exc.message());
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

        local_destructor clear_reentry { [&]
        {
            reentry.clear();
        } };

        auto* const t = current_thread();
        auto* const ti = get_info(t);
        current_exception = info;

        try
        {
            const bool is_debug_exception = (exc == exception_num::breakpoint) | (exc == exception_num::trap);
            int signal = current_signal;

            if ((signal != -1) & is_debug_exception)
            {
                current_signal = -1;
                if (debugmsg)
                    fmt::print(stderr, "break with signal 0x{:0>2x}\n", signal);
            }
            else signal = exc;

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
                               t->id, t->get_name(), exc.message());
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

                if (ti->watchpoints.none() & not ti->stepping)
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
                print("Thread {:d}: Trap masked at {:#x}, resuming with SIGCONT.\n",
                      t->id, std::uintptr_t { f->fault_address.offset });

                leave();
                f->flags.trap = false;
                return true;
            }
            ti->trap_mask = 0;

            if (ti->signal == -1)
                goto done;

            if (not is_debug_exception)
                print("Exception 0x{:x}: {} (error code: 0x{:0>8x})\n",
                      exc.value, exc.message(), std::uint32_t { f->error_code });

            if (debugmsg)
            {
                static_cast<const dpmi10_exception_frame*>(f)->print();
                r->print();
            }

            if (config::enable_gdb_interrupts and f->flags.interrupts_enabled)
                asm("sti");

            do
            {
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
                } while (ti->stopped or packet_available());
            } while (ti->invalid_signal);

            com.flush();
        }
        catch (...)
        {
            fmt::print(stderr, "Exception occured while communicating with GDB.\n"
                               "last received packet: \"{}\"\n",
                       current_packet());
            print_exception();
            halt();
        }
        asm ("cli");

    done:
        const bool ignore_signal = ti->ignore_signal | not is_fault_signal(ti->signal);
        ti->signal = -1;
        ti->watchpoints.reset();
        f->flags.trap = ti->stepping;
        leave();

        return ignore_signal;
    }

    void create_thread(thread* t)
    {
        if (not debug())
            return;

        t->debug_info = new (locked) thread_info { };

        if (thread_events_enabled)
            break_with_signal(debug_signals::thread_started);
    }

    void destroy_thread(thread* t)
    {
        if (thread_events_enabled)
            break_with_signal(debug_signals::thread_finished);

        if (auto* const ti = detail::get_info(t))
        {
            std::atomic_ref { t->debug_info } = nullptr;
            delete ti;
        }

        if (gdb and gdb->query_thread == t)
            gdb->query_thread = nullptr;
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
            t->debug_info = ti;
        }

        gdb = new (locked) gdbstub { cfg };

        for (int s : { SIGHUP, SIGABRT, SIGTERM, SIGKILL, SIGQUIT, SIGILL, SIGINT })
            gdb->signal_handlers[s] = std::signal(s, csignal);

        debug_mode = true;
    }

    static void uninstall_gdb_interface()
    {
        trap_mask no_step { };
        debug_mode = false;
        thread_events_enabled = false;
        if (gdb)
            delete gdb;
        gdb = nullptr;
    }

    void notify_gdb_exit(byte result)
    {
        if (not gdb)
            return;

        trap_mask no_step { };
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
        if (detail::reentry.test())
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
            if ((ti->signal != -1) | ti->stepping)
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
