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

// --- Nested interrupts:
// CWSDPMI: switches to its locked stack on the first interrupt, a nested interrupt calls
// the handler on the current stack (which should already be locked).
// When a hardware exception occurs and interrupts nest 5 levels deep, it crashes? (exphdlr.c:306)

// HDPMI: does have a "locked" stack (LPMS), not sure why. It doesn't even support virtual memory.
// Also switches to the locked stack only on first interrupt, just like CWSDPMI.

// --- Precautions:
// Lock all static code and data with _CRT0_FLAG_LOCK_MEMORY. (main() does this for you)
// Lock dynamically allocated memory with dpmi::class_lock or dpmi::data_lock.
// For STL containers, use dpmi::locking_allocator (read-only) or dpmi::locked_pool_allocator (read/write).

#define INTERRUPT [[gnu::hot, gnu::optimize("O3")]]

namespace jw::dpmi
{
    // Main IRQ handler class
    class irq_handler : public class_lock<irq_handler>
    {
    public:
        template<typename F>
        irq_handler(F&& func, irq_config_flags flags = { }) : function { std::forward<F>(func) }, flags { flags } { }
        ~irq_handler() { disable(); }

        void set_irq(irq_level i) { disable(); irq = i; }
        void enable() { if (not enabled) detail::irq_controller::add(irq, this); enabled = true; }
        void disable() { if (enabled) detail::irq_controller::remove(irq, this); enabled = false; }
        bool is_enabled() const noexcept { return enabled; }

        // Call this from your interrupt handler to signal that the IRQ has been successfully handled.
        static void acknowledge() noexcept { detail::irq_controller::acknowledge(); }

    private:
        friend struct detail::irq_controller;
        irq_handler(const irq_handler&) = delete;
        irq_handler(irq_handler&&) = delete;
        const trivial_function<void()> function;
        const irq_config_flags flags;
        irq_level irq { };
        bool enabled { false };
    };
}
