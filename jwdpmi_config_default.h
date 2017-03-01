#pragma once
#include <jw/typedef.h>

namespace jw
{
    namespace config
    {
        const std::size_t interrupt_stack_size = 16_KB;
        const std::size_t interrupt_initial_stack_pool = 16;
        const std::size_t interrupt_memory_pool = 1_MB;
    }
}