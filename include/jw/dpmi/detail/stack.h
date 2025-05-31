/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2025 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "jwdpmi_config.h"

namespace jw::dpmi::detail
{
    alignas (0x10) inline constinit std::array<std::byte, config::locked_stack_size> locked_stack;
    inline constinit std::uint32_t locked_stack_use_count = 0;

    [[gnu::no_caller_saved_registers, gnu::cdecl]]
    inline std::byte* get_locked_stack() noexcept
    {
        // If an IRQ/exception switches away from this stack, and a nested
        // IRQ/exception occurs, the stack is split in half.  This should be
        // very unlikely to happen.
        const auto n = locked_stack_use_count++;
        return locked_stack.data() + (locked_stack.size() >> n) - 4;
    }
}
