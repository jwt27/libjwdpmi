#pragma once
#include <cstdint>

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            extern volatile std::uint32_t interrupt_count;
            extern volatile std::uint32_t exception_count;
            //extern auto irq::in_service();
        }

        inline bool in_irq_context() noexcept;   // TODO: these
        inline void throw_if_irq();

        inline bool in_interrupt_context() noexcept { return detail::interrupt_count > 0; };
        inline void throw_if_interrupt() { if (in_interrupt_context()) throw std::runtime_error("called from interrupt"); }; // TODO: specialized exception
    }
}