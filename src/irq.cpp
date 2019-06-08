/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

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
            void irq_controller::interrupt_entry_point(int_vector vec) noexcept
            {
                ++interrupt_count;
                fpu_context_switcher.enter(0);
                interrupt_id::push_back(vec, interrupt_id::id_t::interrupt);
                
                byte* esp; asm("mov %0, esp;":"=rm"(esp));
                if (__builtin_expect(static_cast<std::size_t>(esp - data->stack.data()) <= config::interrupt_minimum_stack_size, false))
                {
                    data->increase_stack_size->start();
                }

                auto exception_msg = [] { std::cerr << "EXCEPTION AT INTERRUPT ENTRY POINT" << std::endl; };
                auto hang = [] { do { } while (true); };

                auto i = vec_to_irq(vec);
                if ((i == 7 || i == 15) && !in_service()[i]) goto spurious;

                try
                {
                    std::unique_ptr<irq_mask> mask;
                    if (!(data->entries.at(vec)->flags & no_interrupts)) asm("sti");
                    else if (data->entries.at(vec)->flags & no_reentry) mask = std::make_unique<irq_mask>(i);
                    if (!(data->entries.at(vec)->flags & no_auto_eoi)) send_eoi();
                
                    data->entries.at(vec)->call();
                }
                catch (const std::exception& e) { exception_msg(); print_exception(e); hang(); }
                catch (...) { exception_msg(); hang(); }

                spurious:
                asm("cli");
                acknowledge();
                interrupt_id::pop_back();
                fpu_context_switcher.leave();
                --interrupt_count;
            }

            void irq_controller::call()
            {
                auto exception_msg = [this] { std::cerr << "EXCEPTION IN INTERRUPT HANDLER " << std::hex << vec << std::endl; };
                auto hang = [] { do { } while (true); };
                for (auto f : handler_chain)
                {
                    try
                    {
                        if (f->flags & always_call || !is_acknowledged()) f->handler_ptr();
                    }
                    catch (const std::exception& e) { exception_msg(); print_exception(e); hang(); }
                    catch (...) { exception_msg(); hang(); }
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
                    "sub esp, 0xc;"
                    "push cs:[esi-0x1C];"           // Pass our interrupt vector
                    "call cs:[esi-0x10];"           // Call the entry point
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

                auto* ptr = linear_memory(get_cs(), start, size).get_ptr<byte>();
                std::copy_n(ptr, size, code.data());
                auto cs_limit = reinterpret_cast<std::size_t>(code.data() + size);
                if (descriptor::get_limit(get_cs()) < cs_limit) 
                    descriptor::set_limit(get_cs(), cs_limit);

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
                return data->stack.data() + (data->stack.size() >> (data->stack_use_count++)) - 4;
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
                if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
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
                if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
                return ptr;
            }
        }
    }
}
