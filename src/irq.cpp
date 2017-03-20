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

#include <algorithm>
#include <jw/dpmi/irq.h>
#include <jw/dpmi/fpu.h>
#include <jw/alloc.h>

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            volatile std::uint32_t interrupt_count { 0 };
            std::vector<int_vector> irq_controller::current_int { };
            std::unordered_map<int_vector, irq_controller, std::hash<int_vector>, std::equal_to<int_vector>, locking_allocator<>> irq_controller::entries { };
            std::vector<byte, locking_allocator<>> irq_controller::stack { };
            std::uint32_t irq_controller::stack_use_count { };
            constexpr io::io_port<byte> irq_controller::pic0_cmd;
            constexpr io::io_port<byte> irq_controller::pic1_cmd;
            irq_controller::initializer irq_controller::init { };
            thread::task<void()> irq_controller::increase_stack_size { []() { stack.resize(stack.size() * 2); } };

            void irq_controller::interrupt_entry_point(int_vector vec) noexcept
            {
                ++interrupt_count;
                current_int.push_back(vec);
                fpu_context_switcher.enter();
                
                byte* esp; asm("mov %0, esp;":"=rm"(esp));
                if (static_cast<std::size_t>(esp - stack.data()) <= config::interrupt_minimum_stack_size) increase_stack_size->start();

                auto i = vec_to_irq(vec);
                if ((i == 7 || i == 15) && !in_service()[i]) goto spurious;

                try
                {
                    std::unique_ptr<irq_mask> mask;
                    if (!(entries.at(vec).flags & no_interrupts)) asm("sti");
                    else if (entries.at(vec).flags & no_reentry) mask = std::make_unique<irq_mask>(i);
                    if (!(entries.at(vec).flags & no_auto_eoi)) send_eoi();
                
                    entries.at(vec)();
                }
                catch (...) { std::cerr << "OOPS" << std::endl; } // TODO: exception handling

                spurious:
                asm("cli");
                acknowledge();
                fpu_context_switcher.leave();
                --interrupt_count;
                current_int.pop_back();
            }

            void irq_controller::operator()()
            {
                for (auto f : handler_chain)
                {
                    try
                    {
                        if (f->flags & always_call || !is_acknowledged()) f->handler_ptr(acknowledge);
                    }
                    catch (...) { std::cerr << "EXCEPTION OCCURED IN INTERRUPT HANDLER " << std::hex << vec << std::endl; } // TODO: exceptions
                }
                if (flags & always_chain || !is_acknowledged())
                {
                    interrupt_mask no_ints_here { };
                    call_far_iret(old_handler);
                }
            }

            irq_wrapper::irq_wrapper(int_vector _vec, entry_fptr entry_f, stack_fptr stack_f, std::uint32_t* use_cnt_ptr) noexcept 
                : use_cnt(use_cnt_ptr), get_stack(stack_f), vec(_vec), entry_point(entry_f)
            {
                byte* start;
                std::size_t size;
                asm volatile (
                    "jmp interrupt_wrapper_end%=;"
                    // --- \/\/\/\/\/\/ --- //
                    "interrupt_wrapper_begin%=:"

                    "push ds; push es; push fs; push gs; pusha;"    // 7 bytes
                    "call get_eip%=;"  // call near/relative (E8)   // 5 bytes
                    "get_eip%=: pop esi;"
                    "mov ds, cs:[esi-0x18];"        // Restore segment registers
                    "mov es, cs:[esi-0x16];"
                    "mov fs, cs:[esi-0x14];"
                    "mov gs, cs:[esi-0x12];"
                    "mov ebp, esp;"
                    "mov bx, ss;"
                    "cmp bx, cs:[esi-0x26];"
                    "je keep_stack%=;"
                    "call cs:[esi-0x20];"           // Get a new stack pointer
                    "mov ss, cs:[esi-0x26];"
                    "mov esp, eax;"
                    "keep_stack%=:"
                    "and esp, -0x10;"               // Align stack
                    "sub esp, 0x8;"
                    "push cs:[esi-0x1C];"           // Pass our interrupt vector
                    "call cs:[esi-0x10];"           // Call the entry point
                    "add esp, 0xc;"
                    "cmp bx, cs:[esi-0x26];"
                    "je ret_same_stack%=;"
                    "mov eax, cs:[esi-0x24];"
                    "dec dword ptr [eax];"          // --stack_use_count
                    "mov ss, bx;"         
                    "ret_same_stack%=:"
                    "mov esp, ebp;"
                    "popa; pop gs; pop fs; pop es; pop ds;"
                    "sti;"
                    "iret;"

                    "interrupt_wrapper_end%=:"
                    // --- /\/\/\/\/\/\ --- //
                    "mov %0, offset interrupt_wrapper_begin%=;"
                    "mov %1, offset interrupt_wrapper_end%=;"
                    "sub %1, %0;"                   // size = end - begin
                    : "=rm,r" (start)
                    , "=r,rm" (size)
                    ::"cc");
                assert(size <= code.size());

                auto* ptr = memory_descriptor(get_cs(), start, size).get_ptr<byte>();
                std::copy_n(ptr, size, code.data());

                asm volatile (
                    "mov %w0, ds;"
                    "mov %w1, es;"
                    "mov %w2, fs;"
                    "mov %w3, gs;"
                    "mov %w4, ss;"
                    : "=m" (ds)
                    , "=m" (es)
                    , "=m" (fs)
                    , "=m" (gs)
                    , "=m" (ss));
            }

            byte* irq_controller::get_stack_ptr() noexcept
            {
                return stack.data() + (stack.size() >> (stack_use_count++)) - 4;
            }

            void irq_controller::set_pm_interrupt_vector(int_vector v, far_ptr32 ptr)
            {
                dpmi_error_code error;
                bool c;
                asm volatile (
                    "int 0x31;"
                    : "=@ccc" (c)
                    , "=a" (error)
                    : "a" (0x0205)
                    , "b" (v)
                    , "c" (ptr.segment)
                    , "d" (ptr.offset));
                if (c) throw dpmi_error(error, __FUNCTION__);
            }

            far_ptr32 irq_controller::get_pm_interrupt_vector(int_vector v)
            {
                dpmi_error_code error;
                far_ptr32 ptr;
                bool c;
                asm("int 0x31;"
                    : "=@ccc" (c)
                    , "=a" (error)
                    , "=c" (ptr.segment)
                    , "=d" (ptr.offset)
                    : "a" (0x0204)
                    , "b" (v));
                if (c) throw dpmi_error(error, __FUNCTION__);
                return ptr;
            }
        }
    }
}
