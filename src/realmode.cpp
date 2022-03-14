/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <jw/dpmi/realmode.h>
#include <jw/dpmi/detail/interrupt_id.h>

namespace jw::dpmi::detail
{
    struct rm_int_callback
    {
        rm_int_callback(std::uint8_t i) : raw_handler { i, callback.pointer() } { }

        realmode_interrupt_handler* last { nullptr };

    private:
        realmode_callback callback { [this](realmode_registers* reg, far_ptr32 stack) { handle(reg, stack); }, true };
        raw_realmode_interrupt_handler raw_handler;

        void handle(realmode_registers* reg, far_ptr32 stack)
        {
            force_frame_pointer();
            for (auto i = last; i != nullptr; i = i->prev)
            {
                if (i->func(reg, stack)) return;
            }
            auto chain_to = raw_handler.previous_handler();
            asm volatile
            (R"(
                push fs
                mov fs, %w[ss]
                mov word ptr fs:[%[sp] + 0], %w[ip]
                mov word ptr fs:[%[sp] + 2], %w[cs]
                mov word ptr fs:[%[sp] + 4], %w[flags]
                pop fs
             )" :
                :   [ss]    "rm"    (stack.segment),
                    [sp]    "r"     (stack.offset),
                    [cs]    "r"     (reg->cs),
                    [ip]    "r"     (reg->ip),
                    [flags] "r"     (reg->flags)
            );
            reg->sp -= 6;
            reg->cs = chain_to.segment;
            reg->ip = chain_to.offset;
            reg->flags.interrupt = false;
            reg->flags.trap = false;
        }
    };
}

namespace jw
{
    namespace dpmi
    {
        static constinit std::optional<std::map<std::uint8_t, raw_realmode_interrupt_handler*>> rm_int_handlers { std::nullopt };
        static constinit std::optional<std::map<std::uint8_t, detail::rm_int_callback>> rm_int_callbacks { std::nullopt };

        static far_ptr16 allocate_rm_callback(far_ptr32 func, realmode_registers* reg)
        {
            far_ptr16 ptr;
            dpmi::dpmi_error_code error;
            bool c;
            asm volatile
            (R"(
                push ds
                push es
                push ds; pop es
                mov ds, %w[seg]
                int 0x31
                pop es
                pop ds
             )" : "=@ccc" (c)
                    , "=a" (error)
                    , "=c" (ptr.segment)
                    , "=d" (ptr.offset)
                    : "a" (0x0303)
                    , [seg] "rm" (func.segment)
                    , "S" (func.offset)
                    , "D" (reg)
            );
            if (c) throw dpmi_error { error, __PRETTY_FUNCTION__ };
            return ptr;
        }

        raw_realmode_callback::raw_realmode_callback(far_ptr32 func)
            : ptr { allocate_rm_callback(func, &reg) } { }

        raw_realmode_callback::~raw_realmode_callback()
        {
            dpmi::dpmi_error_code error;
            bool c;
            asm volatile
            (
                "int 0x31"
                : "=@ccc" (c)
                , "=a" (error)
                : "a" (0x0304)
                , "c" (ptr.segment)
                , "d" (ptr.offset)
            );
            if (c) [[unlikely]]
            {
                std::cerr << dpmi_error { error, __PRETTY_FUNCTION__ }.what() << '\n';
                terminate();
            }
        }

        void realmode_callback::call(realmode_callback* self, std::uintptr_t stack_offset, selector stack_selector) noexcept
        {
            detail::interrupt_id id { 0, self->is_irq ? detail::interrupt_type::realmode_irq : detail::interrupt_type::realmode };

            allocator alloc { &self->memres };
            auto* const reg = self->reg_ptr;
            allocator_traits::construct(alloc, reg, self->reg);
            self->reg_ptr = allocator_traits::allocate(alloc, 1);

            if (not self->is_irq) asm ("sti");

            far_ptr32 stack { stack_selector, stack_offset };
            try
            {
                self->func(reg, stack);
            }
            catch (...)
            {
                std::cerr << "Caught exception in real-mode callback handler!\n";
                std::cerr << "Callback pointer: " << self->ptr << '\n';
                std::cerr << "Exception: ";
                try { throw; }
                catch (const std::exception& e) { std::cerr << e.what() << '\n'; }
                catch (...) { std::cerr << "Unknown exception.\n"; }
                reg->flags.carry = true;
            }

            asm ("cli");
            allocator_traits::deallocate(alloc, self->reg_ptr, 1);
            self->reg_ptr = reg;
        }

        template<bool iret_frame>
        void realmode_callback::entry_point() noexcept
        {
#           pragma GCC diagnostic push
#           pragma GCC diagnostic ignored "-Winvalid-offsetof"  // It just works.
#           define OFFSET(X) offsetof(realmode_callback, X)
            asm
            (R"(
                # on entry here:
                # DS:ESI = real-mode stack pointer
                # ES:EDI = real-mode registers struct
                lodsw
                mov word ptr es:[edi + 0x2a], ax    # return IP
                lodsw
                mov word ptr es:[edi + 0x2c], ax    # return CS
            .if %[iret_frame]
                lodsw
                mov word ptr es:[edi + 0x20], ax    # flags
            .endif
                add word ptr es:[edi + 0x2e], 4 + 2 * %[iret_frame]
                lea eax, [edi - %[self_offset]]     # points to 'this'
                mov ebp, esp
                mov cx, es
                mov dx, ds
                mov bx, ss
                mov ds, cx
                mov fs, dx
                mov gs, word ptr [eax + %[gs]]
                cmp bx, cx
                je L%=keep_stack
                mov ss, cx
                mov esp, [eax + %[stack_ptr]]
            L%=keep_stack:
                mov edi, [eax + %[reg_ptr]]     # Pointer to temporary register struct
                and esp, -0x10                  # Align stack
                push edx                        # Real-mode stack selector
                push esi                        # Real-mode stack pointer
                push eax                        # Pointer to self
                call %[callback]
                mov ss, bx
                mov esp, ebp
                iret
             )" : : [iret_frame]    "i" (iret_frame),
                    [self_offset]   "i" (OFFSET(reg)),
                    [stack_ptr]     "i" (OFFSET(stack_ptr)),
                    [reg_ptr]       "i" (OFFSET(reg_ptr)),
                    [gs]            "i" (OFFSET(gs)),
                    [callback]      "i" (call)
            );
#           undef OFFSET
#           pragma GCC diagnostic pop
        }

        template void realmode_callback::entry_point<true>();
        template void realmode_callback::entry_point<false>();

        raw_realmode_interrupt_handler::raw_realmode_interrupt_handler(std::uint8_t i, far_ptr16 ptr)
            : int_num { i }, prev_handler { get(i) }
        {
            if (not rm_int_handlers.has_value()) [[unlikely]] rm_int_handlers.emplace();
            auto*& pos = (*rm_int_handlers)[i];
            prev = pos;
            if (prev != nullptr) prev->next = this;
            pos = this;
            set(i, ptr);
        }

        raw_realmode_interrupt_handler::~raw_realmode_interrupt_handler()
        {
            if (next != nullptr)    // middle of chain
            {
                next->prev = prev;
                next->prev_handler = prev_handler;
            }
            else                    // last in chain
            {
                (*rm_int_handlers)[int_num] = prev;
                set(int_num, prev_handler);
            }
        }

        far_ptr16 raw_realmode_interrupt_handler::get(std::uint8_t i)
        {
            far_ptr16 ptr;
            asm ("int 0x31"
                : "=c" (ptr.segment)
                , "=d" (ptr.offset)
                : "a" (0x0200)
                , "b" (i));
            return ptr;
        }

        void raw_realmode_interrupt_handler::set(std::uint8_t i, far_ptr16 ptr)
        {
            asm("int 0x31"
                :
                : "a" (0x0201)
                , "b" (i)
                , "c" (ptr.segment)
                , "d" (ptr.offset));
        }

        void realmode_interrupt_handler::init()
        {
            if (not rm_int_callbacks.has_value()) [[unlikely]] rm_int_callbacks.emplace();
            auto& pos = rm_int_callbacks->try_emplace(int_num, int_num).first->second;
            prev = pos.last;
            if (prev != nullptr) prev->next = this;
            pos.last = this;
        }

        realmode_interrupt_handler::~realmode_interrupt_handler()
        {
            if (next == nullptr)
            {
                auto i = rm_int_callbacks->find(int_num);
                if (prev == nullptr) rm_int_callbacks->erase(i);
                else i->second.last = prev;
            }
            else next->prev = prev;
        }
    }
}
