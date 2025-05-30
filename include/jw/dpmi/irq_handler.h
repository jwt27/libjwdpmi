/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2017 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <jw/dpmi/detail/irq_controller.h>
#include <jw/dpmi/irq_config_flags.h>
#include <jw/common.h>
#include <array>

// --- --- --- Some notes on DPMI host behaviour: --- --- --- //
// Default RM handlers for INT 0x1C, 0x23, 0x24, and all IRQs reflect to PM, if a PM handler is installed.
// Default PM handlers for all interrupts reflect to RM.

// --- Precautions:
// Lock all static code and data with _CRT0_FLAG_LOCK_MEMORY. (main() does this for you)
// Allocate dynamic memory with 'new (jw::locked) T { };'
// For STL containers, use dpmi::locking_allocator (read-only) or dpmi::locked_pool_allocator (read/write).

namespace jw::dpmi
{
    // Main IRQ handler class
    struct irq_handler final
    {
        template<typename F>
        irq_handler(irq_level i, F&& func, irq_config_flags flags = { })
            : irq_handler { std::forward<F>(func), flags }
        {
            detail::irq_controller::assign(data.get(), i);
        }

        template<typename F>
        irq_handler(F&& func, irq_config_flags flags = { })
            : data { new (locked) detail::irq_handler_data { std::forward<F>(func), flags } } { }

        ~irq_handler()
        {
            detail::irq_controller::remove(data.get());
        }

        template<typename F>
        irq_handler& operator=(F&& func)
        {
            data->set_func(std::forward<F>(func));
            return *this;
        }

        void assign(irq_level i) { detail::irq_controller::assign(data.get(), i); }
        void enable() { detail::irq_controller::enable(data.get()); }
        void disable() { detail::irq_controller::disable(data.get()); }
        bool enabled() const noexcept { return data->is_enabled(); }

        // Call this from your interrupt handler to signal that the IRQ has been successfully handled.
        static void acknowledge() noexcept { detail::irq_controller::acknowledge(); }

        // When the IRQ number is known at compile time, this is faster than the above.
        template<std::uint8_t irq>
        static void acknowledge() noexcept { detail::irq_controller::acknowledge<irq>(); }

    private:
        irq_handler(irq_handler&&) = delete;
        irq_handler(const irq_handler&) = delete;
        irq_handler& operator=(irq_handler&&) = delete;
        irq_handler& operator=(const irq_handler&) = delete;

        std::unique_ptr<detail::irq_handler_data> data;
    };

    struct irq_stats_t
    {
        struct per_irq
        {
            // Minimum and maximum time spent in this interrupt handler, in
            // CPU cycles.  Use chrono::tsc::to_duration() to convert to
            // nanoseconds.
            std::uint32_t min, max;

            // Average duration of the last 32 interrupts, in CPU cycles.
            std::uint32_t avg;

            // Number of times this interrupt was triggered.
            std::uint64_t count;
        };
        std::array<per_irq, 16> irq;

        // Number of spurious interrupts (only collected if IRQ 7 or 15 are
        // hooked).
        std::uint32_t spurious;
    };

    irq_stats_t irq_stats() noexcept;
}
