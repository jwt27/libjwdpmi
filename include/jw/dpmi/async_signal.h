/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <array>
#include <optional>
#include <jw/dpmi/cpu_exception.h>
#include <jw/function.h>

namespace jw::dpmi::detail
{
    bool handle_async_signal(const exception_info&);
}

namespace jw::dpmi
{
    // An async_signal is used when you need to access the application stack
    // or registers from an IRQ handler.  This isn't normally possible under
    // DPMI, but we can simulate it by triggering an exception after returning
    // from the IRQ context.
    // Calling raise() on an async_signal invalidates the application's stack
    // and data segments.  The IRQ handler itself is not affected as it uses
    // different segment selectors.  After returning from the IRQ handler, the
    // first memory access will then trigger a GP fault, where the signal
    // handler is invoked.  If the IRQ was triggered while executing external
    // code, or nested in another interrupt or exception, there will be some
    // delay before the signal occurs.
    // This trick was borrowed from djgpp's libc, where it is used to
    // implement SIGALRM and SIGPROF.
    struct async_signal
    {
        using id_type = unsigned;
        using function_type = void(const exception_info&);

        static constexpr std::size_t max_signals = 32;

        template<typename F>
        async_signal(F&& f) { slots[id] = std::forward<F>(f); }
        ~async_signal();

        async_signal(async_signal&&) = delete;
        async_signal(const async_signal&) = delete;
        async_signal& operator=(async_signal&&) = delete;
        async_signal& operator=(const async_signal&) = delete;

        const id_type id { allocate_id() };

        void raise() const { raise(id); }
        static void raise(id_type);

    private:
        friend bool detail::handle_async_signal(const exception_info&);
        static id_type allocate_id();
        static inline constinit std::array<jw::function<function_type>, max_signals> slots { };
    };
}
