/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/dpmi/detail/irq_controller.h>
#include <jw/dpmi/irq_config_flags.h>
#include <jw/common.h>

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
    class irq_handler
    {
    public:
        template<typename F>
        irq_handler(irq_level i, F&& func, irq_config_flags flags = { })
            : data { new (locked) detail::irq_handler_data { std::forward<F>(func), flags, i } } { }

        template<typename F>
        irq_handler(F&& func, irq_config_flags flags = { }) : irq_handler { 0, std::forward<F>(func), flags } { }

        ~irq_handler() { disable(); }

        void set_irq(irq_level i) { disable(); data->irq = i; }
        void enable() { if (not enabled) { detail::irq_controller::add(data.get()); enabled = true; } }
        void disable() { if (enabled) { detail::irq_controller::remove(data.get()); enabled = false; } }
        bool is_enabled() const noexcept { return enabled; }

        // Call this from your interrupt handler to signal that the IRQ has been successfully handled.
        static void acknowledge() noexcept { detail::irq_controller::acknowledge(); }

    private:
        irq_handler(irq_handler&&) = delete;
        irq_handler(const irq_handler&) = delete;
        irq_handler& operator=(irq_handler&&) = delete;
        irq_handler& operator=(const irq_handler&) = delete;

        std::unique_ptr<detail::irq_handler_data> data;
        bool enabled { false };
    };
}
