/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstdint>
#include <stdexcept>

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            extern volatile std::uint32_t interrupt_count;
            extern volatile std::uint32_t exception_count;
        }

        struct bad_irq_function_call : public std::runtime_error
        {
            bad_irq_function_call() : std::runtime_error("Illegal function call from interrupt routine.") { }
        };
        
        // Returns true if currently in irq or exception context.
        inline bool in_irq_context() noexcept { return __builtin_expect(detail::interrupt_count > 0 || detail::exception_count > 0, false); }

        // Throws bad_irq_function_call if currently in irq or exception context.
        inline void throw_if_irq() { if (in_irq_context()) throw bad_irq_function_call { }; };
    }
}
