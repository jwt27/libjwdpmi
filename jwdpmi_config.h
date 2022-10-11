#pragma once
#include <crt0.h>
#include <jw/common.h>
#include <jw/simd_flags.h>

#if __has_include(<tcp.h>)
# define JWDPMI_WITH_WATT32
#endif

namespace jw
{
    namespace chrono
    {
        struct pit;
        struct tsc;
        struct rtc;
    }

    namespace config
    {
        // Additional startup flags for the djgpp runtime library.
        // See http://www.delorie.com/djgpp/doc/libc/libc_124.html
        constexpr int user_crt0_startup_flags = 0;

        // Initial stack size for IRQ handlers.
        constexpr std::size_t interrupt_initial_stack_size = 64_KB;

        // Minimum stack size for IRQ handlers.  Attempts to resize when the
        // stack space drops below this amount.  When set to 0, automatic
        // resizing is disabled.
        constexpr std::size_t interrupt_minimum_stack_size = 16_KB;

        // Initial size for the global locked memory pool.  This is used by
        // 'operator new' when in interrupt context, but can also be allocated
        // from directly via 'operator new (jw::locked) T'.
        constexpr std::size_t global_locked_pool_size = 1_MB;

        // Total stack size for exception handlers.  Remote debugging requires
        // more stack space.
#       ifndef NDEBUG
        constexpr std::size_t exception_stack_size = 512_KB;
#       else
        constexpr std::size_t exception_stack_size = 64_KB;
#       endif

        // Default stack size for threads.
        constexpr std::size_t thread_default_stack_size = 64_KB;

#       if defined(NDEBUG) and not defined(HAVE__SSE__)
        // If you need to use floating-point instructions in interrupts,
        // exceptions, or realmode callbacks, you must save and restore the
        // FPU registers.  The preferred method is to use dpmi::fpu_context
        // where necessary.  If it is hard to control where FPU instructions
        // may be used, or you want extra safety, these flags may be enabled.
        constexpr bool save_fpu_on_interrupt = false;
        constexpr bool save_fpu_on_exception = false;
        constexpr bool save_fpu_on_realmode_callback = false;
#       else
        // When compiling with -msse or -march=pentium3, it may be difficult to
        // control where the compiler decides to use SSE instructions, so it is
        // safer to leave these enabled.
        // The debugger also requires the FPU context to be saved on exception.
        constexpr bool save_fpu_on_interrupt = true;
        constexpr bool save_fpu_on_exception = true;
        constexpr bool save_fpu_on_realmode_callback = true;
#       endif

        // Set up cpu exception handlers to throw C++ exceptions instead.
        constexpr bool enable_throwing_from_cpu_exceptions = true;

        // Assume memory page size is 4kB.  Use DPMI function 0x0604 otherwise.
        constexpr bool assume_4k_pages = true;

        // Use DPMI function 0x0900 to query and toggle interrupt-enable flag.
        constexpr bool support_virtual_interrupt_flag = false;

        // Enable interrupts while the gdb interface is active
        constexpr bool enable_gdb_interrupts = true;

        // Enable debug messages from the gdb interface
        constexpr bool enable_gdb_debug_messages = false;

        // Display raw packet data from serial gdb interface
        constexpr bool enable_gdb_protocol_dump = false;

        // Clock used for gameport timing.
        using gameport_clock = jw::chrono::tsc;

        // Clock used for midi timestamps.
        using midi_clock = jw::chrono::tsc;

        // Default clock used by yield_for() and yield_while_for().
        using thread_clock = jw::chrono::pit;

        // SIMD instruction set flags that simd_select() is allowed to use.
        constexpr simd allowed_simd = simd::mmx | simd::mmx2 | simd::amd3dnow | simd::amd3dnow2 | simd::sse;
    }
}
