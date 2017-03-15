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

// Hardware exception handling functionality.

#pragma once

#include <iostream>
#include <cstdint>
#include <algorithm>
#include <deque>
#include <function.h>
#include <jw/enum_struct.h>
#include <jw/dpmi/dpmi.h>
#include <jw/dpmi/lock.h>
#include <jw/dpmi/alloc.h>
#include <jw/dpmi/irq_check.h>
#include <jw/dpmi/fpu.h>
#include <../jwdpmi_config.h>

namespace jw
{
    namespace dpmi
    {
        struct[[gnu::packed]] old_exception_frame
        {
            far_ptr32 return_address; unsigned : 16;
            std::uint32_t error_code;
            far_ptr32 fault_address;
            struct[[gnu::packed]] // DPMI 1.0 only
            {
                bool host_exception : 1;
                bool cannot_retry : 1;
                bool redirect_elsewhere : 1;
                unsigned : 13;
            } info_bits;
            struct[[gnu::packed]]
            {
                bool carry : 1;
                unsigned : 1;
                bool parity : 1;
                unsigned : 1;
                bool adjust : 1;
                unsigned : 1;
                bool zero : 1;
                bool sign : 1;
                bool trap : 1;
                bool interrupt : 1;
                bool direction : 1;
                bool overflow : 1;
                unsigned iopl : 2;
                bool nested_task : 1;
                unsigned : 1;
                bool resume : 1;
                bool v86mode : 1;
                bool alignment_check : 1;
                bool virtual_interrupt : 1;
                bool virtual_interrupt_pending : 1;
                bool cpuid_available : 1;
                unsigned : 10;
            } flags;
            far_ptr32 stack; unsigned : 16;
        };
        struct[[gnu::packed]] new_exception_frame : public old_exception_frame
        {
            selector es; unsigned : 16;
            selector ds; unsigned : 16;
            selector fs; unsigned : 16;
            selector gs;
            std::uintptr_t linear_page_fault_address : 32;
            struct[[gnu::packed]]
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
            } page_table_entry;
        };

        struct[[gnu::packed]] raw_exception_frame
        {
            cpu_registers reg;
            old_exception_frame frame_09;
            new_exception_frame frame_10;
        };

        using exception_frame = old_exception_frame; // can be static_cast to new_exception_frame type
        using exception_handler_sig = bool(cpu_registers*, exception_frame*, bool);
        struct exception_num : public enum_struct<std::uint32_t>
        {
            using E = enum_struct<std::uint32_t>;
            using T = typename E::underlying_type;
            enum
            {
                divide_error = 0,
                debug,
                non_maskable_interrupt,
                breakpoint,
                overflow,
                bound_range_exceeded,
                invalid_opcode,
                device_not_present,
                double_fault,
                invalid_tss = 0x0a,
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
            static std::array<byte, config::exception_stack_size> stack; // TODO: allow nested exceptions
            static std::array<std::unique_ptr<std::deque<exception_handler*>>, 0x20> wrapper_list;

            static bool call_handler(exception_handler* self, raw_exception_frame* frame) noexcept;
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
            far_ptr32 previous_handler;                         // 6        1           [eax-0x12]
            std::array<byte, 0x100> code;                       //          1           [eax-0x0C]

        public:
            template<typename F>
            exception_handler(exception_num e, F&& f) : handler(std::allocator_arg, locking_allocator<> { }, std::forward<F>(f)), exc(e), stack_ptr(stack.data() + stack.size())
            {
                init_code();

                if (!wrapper_list[e]) wrapper_list[e] = std::make_unique<std::deque<exception_handler*>>();

                if (wrapper_list[e]->empty()) previous_handler = detail::cpu_exception_handlers::get_pm_handler(e);
                else previous_handler = wrapper_list[e]->back()->get_ptr();
                wrapper_list[e]->push_back(this);

                new_type = detail::cpu_exception_handlers::set_handler(e, get_ptr());
            }

            ~exception_handler();

            exception_handler(const exception_handler&) = delete;
            exception_handler(exception_handler&&) = delete;           
            exception_handler& operator=(const exception_handler&) = delete;
            exception_handler& operator=(exception_handler&&) = delete;

            far_ptr32 get_ptr() const noexcept { return far_ptr32 { get_cs(), reinterpret_cast<std::uintptr_t>(code.data()) }; }
        };
    }
}
