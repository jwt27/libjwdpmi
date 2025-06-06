/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2016 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#pragma once

#include <cstdint>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <optional>
#include <variant>
#include <fmt/core.h>
#include <jw/enum_struct.h>
#include <jw/dpmi/dpmi.h>
#include <jw/dpmi/alloc.h>
#include <jw/dpmi/irq_check.h>
#include <jw/function.h>
#include <jw/debug.h>
#include "jwdpmi_config.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpacked-not-aligned"
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"

namespace jw::dpmi
{
    struct [[gnu::packed]] dpmi09_exception_frame
    {
        far_ptr32 return_address; unsigned : 16;
        std::uint32_t error_code;
        far_ptr32 fault_address;
        union [[gnu::packed]]
        {
            struct [[gnu::packed]] // DPMI 1.0 only
            {
                bool host_exception : 1;
                bool cannot_retry : 1;
                bool redirect_elsewhere : 1;
                unsigned : 13;
            } info_bits;
            std::uint16_t raw_info_bits;
        };
        union
        {
            cpu_flags flags;
            std::uint32_t raw_eflags;
        };
        far_ptr32 stack; unsigned : 16;

        void print(FILE* out = stderr) const
        {
            fmt::print(out, "CPU exception at cs:eip={:0>4x}:{:0>8x}, ss:esp={:0>4x}:{:0>8x}\n"
                            "Error code: {:0>8x}, info bits: {:0>4x}, flags: {:0>8x}\n",
                        fault_address.segment, fault_address.offset, stack.segment, stack.offset,
                        error_code, raw_info_bits, raw_eflags);
        }
    };
    struct [[gnu::packed]] dpmi10_exception_frame : public dpmi09_exception_frame
    {
        selector es { }; unsigned : 16;
        selector ds { }; unsigned : 16;
        selector fs { }; unsigned : 16;
        selector gs { }; unsigned : 16;
        std::uintptr_t cr2 : 32 { };
        union
        {
            struct [[gnu::packed]]
            {
                bool present : 1;
                bool write_access : 1;
                bool user_access : 1;
                bool write_through : 1;
                bool cache_disabled : 1;
                bool accessed : 1;
                bool dirty : 1;
                bool page_attribute_table : 1;
                bool global : 1;
                unsigned reserved : 3;
                unsigned address_high_bits : 20;
            };
            unsigned raw_pte : 32;
            std::uintptr_t physical_address() const noexcept { return raw_pte & 0xfffff000; }
        } page_table_entry { };

        void print(FILE* out = stderr) const
        {
            dpmi09_exception_frame::print();
            fmt::print(out, "ds={:0>4x} es={:0>4x} fs={:0>4x} gs={:0>4x}\n"
                            "[if page fault] Linear: {:0>8x}, PTE: {:0>8x}\n",
                        ds, es, fs, gs,
                        cr2, page_table_entry.raw_pte);
        }
    };

#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Winvalid-offsetof"

    static_assert(offsetof(dpmi09_exception_frame, return_address) == 0);
    static_assert(offsetof(dpmi10_exception_frame, return_address) == 0);
    static_assert(sizeof(dpmi09_exception_frame) == 0x20);
    static_assert(sizeof(dpmi10_exception_frame) == 0x38);

#   pragma GCC diagnostic pop

    using exception_frame = dpmi09_exception_frame; // can be static_cast to dpmi10_exception_frame type

    struct exception_num : public enum_struct<std::uint8_t>
    {
        using E = enum_struct<std::uint8_t>;
        using T = typename E::underlying_type;
        enum : T
        {
            divide_error = 0,
            trap,
            non_maskable_interrupt,
            breakpoint,
            overflow,
            bound_range_exceeded,
            invalid_opcode,
            device_not_available,
            double_fault,
            x87_segment_not_present,
            invalid_tss,
            segment_not_present,
            stack_segment_fault,
            general_protection_fault,
            page_fault,
            x87_exception = 0x10,
            alignment_check,
            machine_check,
            sse_exception,
            virtualization_exception,
            security_exception = 0x1e
        };
        using E::E;
        using E::operator=;

        std::string_view message() const noexcept;
    };

    struct exception_info
    {
        exception_num num;
        cpu_registers* registers;
        exception_frame* frame;
        bool is_dpmi10_frame;
    };

    using exception_handler_sig = bool(const exception_info&);

    // Redirect to the given function on return from an exception handler.
    // Constructs a call frame on the stack so that execution resumes at the
    // fault location when this function returns.  All registers (including
    // FPU and flags) are preserved.
    // Returns false if redirection from this exception frame is not possible.
    bool redirect_exception(const exception_info& info, void(*)());
}

#pragma GCC diagnostic pop

#include <jw/dpmi/detail/cpu_exception.h>

namespace jw::dpmi
{
    struct exception_handler
    {
        template<typename F>
        exception_handler(exception_num n, const F& f)
            : pm { detail::exception_trampoline::create(n, f, false) }
            , rm { nullptr }
        {
            if (pm->is_dpmi10()) try
            {
                rm = detail::exception_trampoline::create(n, f, true);
            }
            catch (const dpmi_error&) { /* ignore */ }
        }

        ~exception_handler()
        {
            if (pm == nullptr) return;
            detail::exception_trampoline::destroy(pm);
            if (rm == nullptr) return;
            detail::exception_trampoline::destroy(rm);
        }

        exception_handler(exception_handler&& other)
            : pm { other.pm }
            , rm { other.rm }
        {
            other.pm = nullptr;
            other.rm = nullptr;
        }

        exception_handler& operator=(exception_handler&& other)
        {
            std::swap(pm, other.pm);
            std::swap(rm, other.rm);
            return *this;
        }

        exception_handler(const exception_handler&) = delete;
        exception_handler& operator=(const exception_handler&) = delete;

    private:
        detail::exception_trampoline* pm;
        detail::exception_trampoline* rm;
    };

    [[noreturn]]
    void throw_cpu_exception(const exception_info&);

    const std::error_category& cpu_category() noexcept;

    struct cpu_exception : public std::system_error
    {
        cpu_registers registers;
        dpmi10_exception_frame frame;
        bool is_dpmi10_frame;
        debug::stacktrace<32> stacktrace;

        void print() const
        {
            if (is_dpmi10_frame) frame.print();
            else static_cast<const dpmi09_exception_frame*>(&frame)->print();
            registers.print();
            stacktrace.print();
        }

    protected:
        friend void throw_cpu_exception(const exception_info&);

        cpu_exception(const exception_info& i)
            : cpu_exception { i.num, i } { }

        cpu_exception(exception_num n, const exception_info& i)
            : cpu_exception { n, i.registers, i.frame, i.is_dpmi10_frame } { }

        cpu_exception(exception_num n, cpu_registers* r, exception_frame* f, bool t)
            : system_error { static_cast<int>(n), cpu_category() }
            , registers { *r }, frame { init_frame(f, t) }, is_dpmi10_frame { t } { }

        static dpmi10_exception_frame init_frame(dpmi09_exception_frame* f, bool t) noexcept
        {
            if (t) return *static_cast<dpmi10_exception_frame*>(f);
            else return { *f };
        }
    };

    template<exception_num N>
    class specific_cpu_exception : public cpu_exception
    {
        friend void throw_cpu_exception(const exception_info&);
        specific_cpu_exception(const exception_info& i) : cpu_exception { N, i } { }
    };

    using divide_error             = specific_cpu_exception<exception_num::divide_error>;
    using trap                     = specific_cpu_exception<exception_num::trap>;
    using non_maskable_interrupt   = specific_cpu_exception<exception_num::non_maskable_interrupt>;
    using breakpoint               = specific_cpu_exception<exception_num::breakpoint>;
    using overflow                 = specific_cpu_exception<exception_num::overflow>;
    using bound_range_exceeded     = specific_cpu_exception<exception_num::bound_range_exceeded>;
    using invalid_opcode           = specific_cpu_exception<exception_num::invalid_opcode>;
    using device_not_available     = specific_cpu_exception<exception_num::device_not_available>;
    using double_fault             = specific_cpu_exception<exception_num::double_fault>;
    using x87_segment_not_present  = specific_cpu_exception<exception_num::x87_segment_not_present>;
    using invalid_tss              = specific_cpu_exception<exception_num::invalid_tss>;
    using segment_not_present      = specific_cpu_exception<exception_num::segment_not_present>;
    using stack_segment_fault      = specific_cpu_exception<exception_num::stack_segment_fault>;
    using general_protection_fault = specific_cpu_exception<exception_num::general_protection_fault>;
    using page_fault               = specific_cpu_exception<exception_num::page_fault>;
    using x87_exception            = specific_cpu_exception<exception_num::x87_exception>;
    using alignment_check          = specific_cpu_exception<exception_num::alignment_check>;
    using machine_check            = specific_cpu_exception<exception_num::machine_check>;
    using sse_exception            = specific_cpu_exception<exception_num::sse_exception>;
    using virtualization_exception = specific_cpu_exception<exception_num::virtualization_exception>;
    using security_exception       = specific_cpu_exception<exception_num::security_exception>;
}
