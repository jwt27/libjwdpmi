#pragma once
#include <jw/typedef.h>

namespace jw
{
    namespace config
    {
        // Additional startup flags for the djgpp runtime library.
        // See http://www.delorie.com/djgpp/doc/libc/libc_124.html
        const int user_crt0_startup_flags = 0;

        const std::size_t interrupt_initial_stack_size = 1_MB;
        const std::size_t interrupt_minimum_stack_size = 16_KB;
        const std::size_t interrupt_memory_pool = 1_MB;
        const std::size_t interrupt_fpu_context_pool = 1_MB;

        const std::size_t exception_stack_size = 32_KB;
    }
}