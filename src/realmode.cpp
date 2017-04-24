/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <jw/dpmi/realmode.h>

namespace jw
{
    namespace dpmi
    {
        void realmode_callback_base::alloc(void* func)
        {
            dpmi::dpmi_error_code error;
            bool c;
            asm volatile(
                "push es;"
                "push ds;"
                "pop es;"
                "int 0x31;"
                "pop es;"
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
                "push ds;"
                "push es;"
                "push ds; pop es;"
                "push cs; pop ds;"
                "int 0x31;"
                "pop es;"
                "pop ds;"
                : "=@ccc" (c)
                , "=a" (error)
                : "a" (0x0304)
                , "c" (ptr.segment)
                , "d" (ptr.offset));
            if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
        }

        void realmode_callback::entry_point(realmode_callback* self, std::uint32_t rm_stack_selector, std::uint32_t rm_stack_offset) noexcept
        {
            auto* reg = self->reg_ptr;
            self->reg_pool.push_back({ });
            self->reg_ptr = &self->reg_pool.back();
            *reg = self->reg;
            if (reg->flags.interrupt) interrupt_mask::sti();

            try
            {
                self->function_ptr(reg, far_ptr32 { static_cast<selector>(rm_stack_selector), rm_stack_offset });
            }
            catch (...) { } // TODO

            asm("cli");
            self->reg_pool.pop_back();
            self->reg_ptr = &self->reg_pool.back();
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
                "get_eip%=: pop eax;"
                "mov dx, ds;"
                "movzx edx, dx;"
                "push es; pop ds;"
                "mov ebp, esp;"
                "mov bx, ss;"
                "mov cx, ds;"
                "cmp bx, cx;"
                "je keep_stack%=;"
                "push ds; pop ss;"
                "mov esp, cs:[eax-0x11];"
                "keep_stack%=:"
                "mov edi, cs:[eax-0x0D];"
                "and esp, -0x10;"               // Align stack
                "push esi;"                     // Real-mode stack offset
                "push edx;"                     // Real-mode stack selector
                "push cs:[eax-0x09];"           // Pointer to self
                "call cs:[eax-0x15];"           // Call the entry point
                "mov cx, ds;"
                "cmp bx, cx;"
                "je ret_same_stack%=;"
                "mov ss, bx;"
                "ret_same_stack%=:"
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
            if (ldt_entry::get_limit(get_cs()) < cs_limit) 
                ldt_entry::set_limit(get_cs(), cs_limit);

            reg_pool.push_back({ });
            reg_ptr = &reg_pool.back();
        }
    }
}
