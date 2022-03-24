/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/dpmi/memory.h>
#include <jw/dpmi/irq_handler.h>
#include <jw/function.h>
#include <jw/allocator_adaptor.h>

namespace jw::dpmi::detail
{
    struct rm_int_callback;
}

namespace jw
{
    namespace dpmi
    {
        // CPU register structure for DPMI real-mode functions.
        struct alignas(2) [[gnu::packed]] realmode_registers : public cpu_registers
        {
            union
            {
                std::uint16_t raw_flags;
                struct [[gnu::packed]]
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
                } flags;
            };
            std::uint16_t es, ds, fs, gs;
            std::uint16_t ip, cs; // not used in call_int().
            std::uint16_t sp, ss; // used in call functions to pass arguments on the stack. set to 0 if not used.

            auto& print(std::ostream& out) const
            {
                using namespace std;
                out << hex << setfill('0');
                out << "es=" << setw(4) << es << " ds=" << setw(4) << ds << " fs=" << setw(4) << fs << " gs=" << setw(4) << gs << "\n";
                out << "cs=" << setw(4) << cs << " ip=" << setw(4) << ip << " ss=" << setw(4) << ss << " sp=" << setw(4) << sp << " flags=" << setw(4) << raw_flags << "\n";
                out << hex << setfill(' ') << setw(0) << flush;
                return out;
            }
            friend auto& operator<<(std::ostream& out, const realmode_registers& in) { return in.print(out); }

            // Call a real-mode interrupt
            void call_int(int_vector interrupt) { call<0x0300>(interrupt); }

            // Call a real-mode procedure which returns with RETF
            // Function address given by CS:IP fields
            void call_far() { call<0x0301>(0); }

            // Call a real-mode procedure which returns with IRET
            // Function address given by CS:IP fields
            void call_far_iret() { call<0x0302>(0); }

            // Call a real-mode procedure which returns with RETF
            void call_far(far_ptr16 ptr) { ip = ptr.offset; cs = ptr.segment; call_far(); }

            // Call a real-mode procedure which returns with IRET
            void call_far_iret(far_ptr16 ptr) { ip = ptr.offset; cs = ptr.segment; call_far_iret(); }

        private:
            template <std::uint16_t dpmi_function>
            void call(int_vector interrupt)
            {
                force_frame_pointer();
                dpmi_error_code error;
                bool c;

                asm volatile
                (R"(
                    push es
                    mov es, %w2
                    int 0x31
                    pop es
                 )" : "=@ccc" (c)
                    , "=a" (error)
                    : "rm" (get_ds())
                    , "a" (dpmi_function)
                    , "b" (interrupt)
                    , "D" (this)
                    , "c" (0)
                    : "memory"
                );
                if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
            }
        };

        static_assert(sizeof(realmode_registers) == 0x32, "check sizeof struct dpmi::realmode_registers");

        // Reference for writing real-mode callback functions:
        // http://www.delorie.com/djgpp/doc/dpmi/ch4.6.html
        struct raw_realmode_callback
        {
            virtual ~raw_realmode_callback();

            raw_realmode_callback(const raw_realmode_callback&) = delete;
            raw_realmode_callback(raw_realmode_callback&&) = delete;
            raw_realmode_callback& operator=(const raw_realmode_callback&) = delete;
            raw_realmode_callback& operator=(raw_realmode_callback&&) = delete;

            far_ptr16 pointer() const noexcept { return ptr; }

        protected:
            raw_realmode_callback(far_ptr32 func);
            realmode_registers reg;
            const far_ptr16 ptr;
        };

        // Allocates a callback function that can be invoked from real-mode.
        // The constructor parameter 'iret_frame' specifies whether the real-
        // mode code should return via IRET or RETF.  Parameter 'irq_context'
        // specifies what dpmi::in_irq_context() returns when this callback
        // is invoked, enable this if your callback will be used from a
        // hardware interrupt handler.
        // The callback function takes a pointer to a registers structure
        // which may be modified, and a far pointer to access the real-mode
        // stack.  The return CS/IP (and flags) will have already been popped
        // off and stored in the registers struct.
        struct realmode_callback final : raw_realmode_callback, private class_lock<realmode_callback>
        {
            using function_type = void(realmode_registers*, __seg_fs void*);

            template<typename F>
            realmode_callback(F&& function, bool iret_frame = false, bool irq_context = false, std::size_t stack_size = 16_KB, std::size_t pool_size = 1_KB)
                : raw_realmode_callback({ get_cs(), reinterpret_cast<std::uintptr_t>(iret_frame ? entry_point<true> : entry_point<false>) })
                , is_irq { irq_context }
                , func { std::forward<F>(function) }
                , memres { pool_size }
            {
                stack.resize(stack_size);
                stack_ptr = stack.data() + stack.size() - 4;
                allocator alloc { &memres };
                reg_ptr = allocator_traits::allocate(alloc, 1);
            }

            bool is_irq;

        private:
            using allocator = monomorphic_allocator<locked_pool_resource<false>, realmode_registers>;
            using allocator_traits = std::allocator_traits<allocator>;

            template<bool>
            [[gnu::naked]] static void entry_point() noexcept;
            [[gnu::__cdecl__]] static void call(realmode_callback*, __seg_fs void*) noexcept;

            std::byte* stack_ptr;
            realmode_registers* reg_ptr;

            trivial_function<function_type> func;
            std::vector<std::byte, default_constructing_allocator_adaptor<locking_allocator<std::byte>>> stack;
            locked_pool_resource<false> memres;
        };

        // Registers a real-mode procedure as real-mode software interrupt
        // handler.  The code must be located in conventional memory.
        struct raw_realmode_interrupt_handler
        {
            raw_realmode_interrupt_handler(std::uint8_t i, far_ptr16 p);
            ~raw_realmode_interrupt_handler();

            raw_realmode_interrupt_handler(raw_realmode_interrupt_handler&&) = delete;
            raw_realmode_interrupt_handler(const raw_realmode_interrupt_handler&) = delete;
            raw_realmode_interrupt_handler& operator=(raw_realmode_interrupt_handler&&) = delete;
            raw_realmode_interrupt_handler& operator=(const raw_realmode_interrupt_handler&) = delete;

            far_ptr16 previous_handler() const noexcept { return prev_handler; }

            static far_ptr16 get(std::uint8_t);

        private:
            const std::uint8_t int_num;
            far_ptr16 prev_handler;
            raw_realmode_interrupt_handler* next { nullptr };
            raw_realmode_interrupt_handler* prev;

            static void set(std::uint8_t, far_ptr16);
        };

        // Registers a procedure as real-mode software interrupt handler,
        // using a callback to protected mode.
        // The handler function returns a bool, indicating whether the
        // interrupt was successfully handled.  If false, the next handler in
        // the chain will be called.
        // This is not suitable for servicing hardware interrupts.  To do
        // that, use dpmi::irq_handler instead.
        struct realmode_interrupt_handler final
        {
            using function_type = bool(realmode_registers*, __seg_fs void*);

            template<typename F>
            realmode_interrupt_handler(std::uint8_t i, F&& f, bool irq_context = false)
                : int_num { i }, func { std::forward<F>(f) }, is_irq { irq_context }
            { init(); }

            ~realmode_interrupt_handler();

            realmode_interrupt_handler(realmode_interrupt_handler&&) = delete;
            realmode_interrupt_handler(const realmode_interrupt_handler&) = delete;
            realmode_interrupt_handler& operator=(realmode_interrupt_handler&&) = delete;
            realmode_interrupt_handler& operator=(const realmode_interrupt_handler&) = delete;

        private:
            friend struct detail::rm_int_callback;
            void init();

            const std::uint8_t int_num;
            const function<function_type> func;
            const bool is_irq;
            realmode_interrupt_handler* next { nullptr };
            realmode_interrupt_handler* prev;
        };
    }
}
