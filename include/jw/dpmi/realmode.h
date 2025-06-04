/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2017 - 2025 J.W. Jagersma, see COPYING.txt for details    */

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

            // Call a real-mode interrupt
            void call_int(std::uint8_t interrupt) { call<0x0300>(interrupt); }

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
            void call(std::uint8_t interrupt)
            {
                std::uint16_t ax { dpmi_function };
                std::uint16_t bx { interrupt };
                bool c;

                asm volatile
                (
                    "int 0x31"
                    : "=@ccc" (c)
                    , "+a" (ax)
                    , "+m" (*this)
                    : "b" (bx)
                    , "c" (0)
                    , "D" (this)
                    : "cc"
                );
                if (c) throw dpmi_error { ax, __PRETTY_FUNCTION__ };
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

        // Configuration options for realmode_callback.
        struct realmode_callback_config
        {
            // Enable this if the real-mode code will be invoked by INT instead
            // of CALL FAR, and thus should return by IRET instead of RETF.
            bool iret_frame = false;

            // Specifies what dpmi::in_irq_context() returns when this callback
            // is invoked.  Enable this if your callback will be invoked from a
            // hardware interrupt handler.
            bool irq_context = false;

            // A new realmode_registers struct must be used for each re-entry
            // into the callback.  This option controls how many are allocated,
            // and so determines how many times the callback may be re-entered.
            std::size_t pool_size = 8;

            // Stack size available to the callback.
            std::size_t stack_size = 16_KB;
        };

        // Allocates a callback function that can be invoked from real-mode.
        // The callback function takes a pointer to a registers structure
        // which may be modified, and a far pointer to access the real-mode
        // stack.  On entry, the return CS/IP (and flags) will have already
        // been popped off and stored in the registers struct.
        struct realmode_callback final
        {
            using function_type = void(realmode_registers*, far_ptr32);

            template<typename F>
            realmode_callback(F&& function, realmode_callback_config cfg = { })
                : data { new (locked) callback_data { std::forward<F>(function), cfg } } { }

            far_ptr16 pointer() const noexcept { return data->pointer(); }

            bool is_irq() const noexcept { return data->is_irq; }
            void is_irq(bool i) noexcept { data->is_irq = i; }

        private:
            template <typename T>
            using allocator = default_constructing_allocator_adaptor<locking_allocator<T>>;

            struct callback_data : raw_realmode_callback
            {
                template<typename F>
                callback_data(F&& function, const realmode_callback_config& cfg)
                    : raw_realmode_callback({ get_cs(), reinterpret_cast<std::uintptr_t>(cfg.iret_frame ? entry_point<true> : entry_point<false>) })
                    , is_irq { cfg.irq_context }
                    , func { std::forward<F>(function) }
                {
                    stack.resize(cfg.stack_size);
                    stack_ptr = stack.data() + stack.size() - 4;
                    reg_pool.resize(cfg.pool_size);
                    reg_ptr = reg_pool.data();
                }

                bool is_irq;

            private:
                template<bool>
                [[gnu::naked]] static void entry_point() noexcept;
                [[gnu::cdecl]] static void call(callback_data*, std::uintptr_t, selector) noexcept;

                std::byte* stack_ptr;
                realmode_registers* reg_ptr;

                function<function_type> func;
                std::vector<std::byte, allocator<std::byte>> stack;
                std::vector<realmode_registers, allocator<realmode_registers>> reg_pool;
            };

            std::unique_ptr<callback_data> data;
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
            using function_type = bool(realmode_registers*, far_ptr32);

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
