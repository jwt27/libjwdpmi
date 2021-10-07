/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

// Hardware exception handling functionality.

#pragma once

#include <iostream>
#include <cstdint>
#include <algorithm>
#include <deque>
#include <stdexcept>
#include <bitset>
#include <string>
#include <function.h>
#include <jw/enum_struct.h>
#include <jw/dpmi/dpmi.h>
#include <jw/dpmi/lock.h>
#include <jw/dpmi/alloc.h>
#include <jw/dpmi/irq_check.h>
#include <jw/dpmi/fpu.h>
#include "jwdpmi_config.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpacked-not-aligned"

namespace jw
{
    namespace dpmi
    {
        struct [[gnu::packed]] old_exception_frame
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

            void print() const
            {
                std::fprintf(stderr, "CPU exception at cs:eip=%.4hx:%.8lx, ss:eip=%.4hx:%.8lx\n",
                    fault_address.segment, fault_address.offset, stack.segment, stack.offset);
                std::fprintf(stderr, "Error code: %.8lx, info bits: %.1hx, flags: %.8lx\n",
                    error_code, raw_info_bits, raw_eflags);
            }
        };
        struct [[gnu::packed]] new_exception_frame : public old_exception_frame
        {
            selector es { }; unsigned : 16;
            selector ds { }; unsigned : 16;
            selector fs { }; unsigned : 16;
            selector gs { }; unsigned : 16;
            std::uintptr_t linear_page_fault_address : 32 { };
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
                    bool global : 1;
                    unsigned reserved : 3;
                    unsigned physical_address : 21;
                };
                unsigned raw_pte : 32;
            } page_table_entry { };

            void print() const
            {
                old_exception_frame::print();
                std::fprintf(stderr, "(if page fault) Linear: %.8lx, physical: %.8lx, PTE: %.1hhx\n",
                    linear_page_fault_address, static_cast<long unsigned int>(page_table_entry.physical_address), page_table_entry.raw_pte);
                std::fprintf(stderr, "ds=%.4hx es=%.4hx fs=%.4hx gs=%.4hx\n",
                    ds, es, fs, gs);
            }
        };

        struct [[gnu::packed]] raw_exception_frame
        {
            cpu_registers reg;
            old_exception_frame frame_09;
            new_exception_frame frame_10;
        };

        static_assert(sizeof(old_exception_frame) == 0x20);
        static_assert(sizeof(new_exception_frame) == 0x38);
        static_assert(sizeof(raw_exception_frame) == 0x78);

        using exception_frame = old_exception_frame; // can be static_cast to new_exception_frame type
        using exception_handler_sig = bool(cpu_registers*, exception_frame*, bool);

        struct exception_num : public enum_struct<std::uint32_t>
        {
            using E = enum_struct<std::uint32_t>;
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
        };
    }
}

#pragma GCC diagnostic pop

#include <jw/dpmi/detail/cpu_exception.h>

namespace jw
{
    namespace dpmi
    {
        class exception_handler : class_lock<exception_handler>
        {
            void init_code();
            func::function<exception_handler_sig> handler;
            exception_num exc;
            exception_handler* next { nullptr };
            exception_handler* prev { nullptr };
            static inline std::array<exception_handler*, 0x20> last { };
            static inline std::array<byte, config::exception_stack_size> stack;

            [[gnu::force_align_arg_pointer, gnu::cdecl]] static bool call_handler(exception_handler* self, raw_exception_frame* frame) noexcept;
                                                                // sizeof   alignof     offset
            exception_handler* self { this };                   // 4        4           [eax-0x28]
            decltype(&call_handler) call_ptr { &call_handler }; // 4        4           [eax-0x24]
            byte* stack_ptr;                                    // 4        4           [eax-0x20]
            selector ds;                                        // 2        2           [eax-0x1C]
            selector es;                                        // 2        2           [eax-0x1A]
            selector fs;                                        // 2        2           [eax-0x18]
            selector gs;                                        // 2        2           [eax-0x16]
            bool new_type;                                      // 1        1           [eax-0x14]
            byte _padding;                                      // 1        1           [eax-0x13]
            far_ptr32 chain_to;                                 // 6        2           [eax-0x12]
            std::array<byte, 0x100> code;                       //          1           [eax-0x0C]

        public:
            template<typename F>    // TODO: real-mode (requires a separate wrapper list)
            exception_handler(exception_num e, F&& f, bool = false)
                : handler(std::allocator_arg, locking_allocator<> { }, std::forward<F>(f))
                , exc(e), stack_ptr(stack.data() + stack.size() - 4)
                , chain_to { detail::cpu_exception_handlers::get_pm_handler(e) }
            {
                detail::setup_exception_throwers();
                init_code();

                prev = last[e];
                if (prev != nullptr) prev->next = this;
                last[e] = this;

                new_type = detail::cpu_exception_handlers::set_pm_handler(e, get_ptr());
            }

            ~exception_handler();

            exception_handler(const exception_handler&) = delete;
            exception_handler(exception_handler&&) = delete;
            exception_handler& operator=(const exception_handler&) = delete;
            exception_handler& operator=(exception_handler&&) = delete;

            far_ptr32 get_ptr() const noexcept { return far_ptr32 { get_cs(), reinterpret_cast<std::uintptr_t>(code.data()) }; }
        };

        struct cpu_category : public std::error_category
        {
            virtual const char* name() const noexcept override { return "CPU"; }
            virtual std::string message(int ev) const override;
        };

        struct cpu_exception : public std::system_error
        {
            const cpu_registers registers;
            const new_exception_frame frame;
            const bool new_frame_type;

            cpu_exception(exception_num n, cpu_registers* r, exception_frame* f, bool t)
                : system_error { static_cast<int>(n), cpu_category { } }
                , registers { *r }, frame { init_frame(f, t) }, new_frame_type { t } { }

            void print() const
            {
                if (new_frame_type) frame.print();
                else static_cast<const old_exception_frame*>(&frame)->print();
                registers.print();
            }

        private:
            static new_exception_frame init_frame(old_exception_frame* f, bool t) noexcept
            {
                if (t) return *static_cast<new_exception_frame*>(f);
                else return { *f };
            }
        };

        template<exception_num N>
        struct specific_cpu_exception : public cpu_exception
        {
            specific_cpu_exception(cpu_registers* r, exception_frame* f, bool t) : cpu_exception { N, r, f, t } { }
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
}
