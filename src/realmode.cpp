/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <jw/dpmi/realmode.h>
#include <jw/dpmi/detail/interrupt_id.h>

namespace jw
{
    namespace dpmi
    {
        void realmode_callback_base::alloc(void* func)
        {
            dpmi::dpmi_error_code error;
            bool c;
            asm volatile(
                "push ds;"
                "push es;"
                "push ds; pop es;"
                "push cs; pop ds;"
                "int 0x31;"
                "pop es;"
                "pop ds;"
                : "=@ccc" (c)
                , "=a" (error)
                , "=c" (ptr.segment)
                , "=d" (ptr.offset)
                : "a" (0x0303)
                , "S" (func)
                , "D" (&reg));
            if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
        }

        void realmode_callback_base::free()
        {
            dpmi::dpmi_error_code error;
            bool c;
            asm volatile(
                "int 0x31;"
                : "=@ccc" (c)
                , "=a" (error)
                : "a" (0x0304)
                , "c" (ptr.segment)
                , "d" (ptr.offset));
            if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
        }

        void realmode_callback::entry_point(realmode_callback* self, std::uint32_t, std::uint32_t) noexcept
        {
            bool is_irq = not self->reg.flags.interrupt;
            detail::interrupt_id id { 0, is_irq ? detail::interrupt_type::realmode_irq : detail::interrupt_type::realmode };

            allocator alloc { &self->memres };
            auto* const reg = self->reg_ptr;
            self->reg_ptr = alloc.allocate(1);
            new (reg) realmode_registers { self->reg };
            if (not is_irq) interrupt_mask::sti();

            auto fail = [reg, self]
            {
                std::cerr << "Caught exception in real-mode callback handler!\n";
                std::cerr << "Callback pointer: " << self->get_ptr() << '\n';
                std::cerr << "Exception: ";
                reg->flags.carry = true;
            };

            try
            {
                self->function_ptr(reg);
            }
            catch (const std::exception& e) { fail(); std::cerr << e.what() << '\n'; }
            catch (...) { fail(); std::cerr << "Unknown exception.\n"; }

            asm("cli");
            alloc.deallocate(self->reg_ptr, 1);
            self->reg_ptr = reg;
        }

        void realmode_callback::init_code() noexcept
        {
            byte* start;
            std::size_t size;
            asm volatile (
                "jmp realmode_callback_wrapper_end%=;"
                // --- \/\/\/\/\/\/ --- //
                "realmode_callback_wrapper_begin%=:"

                // on entry here:
                // DS:ESI = real-mode stack pointer
                // ES:EDI = real-mode registers struct
                "call get_eip%=;"  // call near/relative (E8)   // 5 bytes
                "get_eip%=:"
                "lodsw;"
                "mov word ptr es:[edi+0x2a], ax;"   // real-mode return IP
                "lodsw;"
                "mov word ptr es:[edi+0x2c], ax;"   // real-mode return CS
                "add word ptr es:[edi+0x2e], 4;"    // remove CS/IP from real-mode stack
                "pop eax;"
                "mov dx, ds;"
                "movzx edx, dx;"
                "mov cx, es;"
                "mov ds, cx;"
                "mov fs, word ptr cs:[eax-0x19];"
                "mov gs, word ptr cs:[eax-0x17];"
                "mov ebp, esp;"
                "mov bx, ss;"
                "cmp bx, cx;"
                "je Lkeep_stack%=;"
                "mov ss, cx;"
                "mov esp, cs:[eax-0x11];"
                "Lkeep_stack%=:"
                "mov edi, cs:[eax-0x0D];"       // Pointer to temporary register struct
                "and esp, -0x10;"               // Align stack
                "push esi;"                     // Real-mode stack offset
                "push edx;"                     // Real-mode stack selector
                "push cs:[eax-0x09];"           // Pointer to self
                "call cs:[eax-0x15];"           // Call the entry point
                "mov ss, bx;"
                "mov esp, ebp;"
                "iret;"

                "realmode_callback_wrapper_end%=:"
                // --- /\/\/\/\/\/\ --- //
                "mov %0, offset realmode_callback_wrapper_begin%=;"
                "mov %1, offset realmode_callback_wrapper_end%=;"
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

            reg_ptr = allocator { &memres }.allocate(1);

            asm volatile (
                "mov %w0, fs;"
                "mov %w1, gs;"
                : "=m" (fs)
                , "=m" (gs));
        }
    }
}
