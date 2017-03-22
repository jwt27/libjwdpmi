#pragma once
#include <crt0.h>
#include <jw/typedef.h>

namespace jw
{
    namespace config
    {
        // Additional startup flags for the djgpp runtime library.
        // See http://www.delorie.com/djgpp/doc/libc/libc_124.html
        const int user_crt0_startup_flags = 0;

        // Initial stack size for IRQ handlers.
        const std::size_t interrupt_initial_stack_size = 1_MB;

        // Minimum stack size for IRQ handlers. Tries to resize when the stack size drops below this amount.
        const std::size_t interrupt_minimum_stack_size = 64_KB;

        // Total memory allocated to store fpu contexts.
        const std::size_t interrupt_fpu_context_pool = 32_KB;

        // Initial memory pool for operator new() in interrupt context.
        const std::size_t interrupt_initial_memory_pool = 1_MB;

        // Total stack size for exception handlers. Remote debugging requires a lot of stack space.
        const std::size_t exception_stack_size = 1_MB;

        // Default stack size for threads.
        const std::size_t thread_default_stack_size = 64_KB;

        // Set up cpu exception handlers to throw C++ exceptions instead.
        const bool enable_throwing_from_cpu_exceptions = true;

        // Enable debug messages from the gdb interface
        const bool enable_gdb_debug_messages = false;
    }
}