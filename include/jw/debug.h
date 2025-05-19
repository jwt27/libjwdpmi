/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2017 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <jw/detail/debug.h>
#include <jw/dpmi/memory.h>
#include <fmt/format.h>
#include <limits>
#include <source_location>

namespace jw::debug
{
#ifndef NDEBUG
    // Returns true if in debug mode.
    inline bool debug() noexcept { return detail::debug_mode; }
#else
    constexpr inline bool debug() noexcept { return false; }
#endif

    // Set a breakpoint
    inline void breakpoint()
    {
        if (debug())
            asm("int 3");
    }

    // Set a breakpoint with specified signal.
    // signal can be an exception number, C signal number, or any user-defined
    // signal.
    inline void break_with_signal([[maybe_unused]] int signal)
    {
#ifndef NDEBUG
        detail::current_signal = signal;
        breakpoint();
#endif
    }

    using stacktrace_entry = std::uintptr_t;

    template<std::size_t MaxSize>
    struct stacktrace : detail::stacktrace_base
    {
        stacktrace() noexcept = default;

        static stacktrace current(std::size_t skip = 0)
        {
            stacktrace stk;
            auto* const p = stk.ips.data();
            stk.n = stacktrace_base::make(p, p + MaxSize, skip);
            return stk;
        }

        void print(FILE* file = stderr) const
        {
            stacktrace_base::print(file, entries());
        }

        std::span<const stacktrace_entry> entries() const noexcept
        {
            return { ips.data(), n };
        }

    private:
        std::size_t n = 0;
        std::array<stacktrace_entry, MaxSize> ips;
    };

    struct assertion_failed : public std::runtime_error
    {
        using runtime_error::runtime_error;
    };

#ifndef NDEBUG
    [[gnu::noinline]]
    inline void throw_assert(bool condition, std::source_location loc = std::source_location::current())
    {
        if (not condition)
        {
            breakpoint();
            auto str = fmt::format("Assertion failed at {:p} in function {} ({}:{:d})",
                                   __builtin_return_address(0), loc.function_name(), loc.file_name(), loc.line());
            throw assertion_failed { str };
        }
    }
#else
    inline void throw_assert(bool) { }
#endif

    [[gnu::always_inline]]
    inline void* get_eip() noexcept
    {
        void* eip;
        asm("call L%=; L%=: pop %0" : "=rm" (eip));
        return eip;
    }

    // Disable the trap flag
    struct trap_mask
    {
#ifndef NDEBUG
        trap_mask() noexcept;
        ~trap_mask() noexcept;

        trap_mask(const trap_mask&) = delete;
        trap_mask(trap_mask&&) = delete;
        trap_mask& operator=(const trap_mask&) = delete;
        trap_mask& operator=(trap_mask&&) = delete;
    private:
        bool failed { true };
#else
        constexpr trap_mask() noexcept { }
#endif
    };

    // Set a watchpoint
    // Remember, only 4 watchpoints can exist simultaneously.
    struct watchpoint
    {
        enum watchpoint_type : std::uint8_t
        {
            execute,
            write,
            read_write
        };

        template<typename T> requires (sizeof(T) == 4 or sizeof(T) == 2 or sizeof(T) == 1)
        watchpoint(T* ptr, watchpoint_type t)
            : watchpoint { dpmi::near_to_linear(ptr), sizeof(T), t } { }

        watchpoint(void* ptr, watchpoint_type t, std::size_t size)
            : watchpoint { dpmi::near_to_linear(ptr), size, t } { }

#ifndef NDEBUG
        watchpoint(const watchpoint&) = delete;
        watchpoint& operator=(const watchpoint&) = delete;

        watchpoint(watchpoint&& m)
            : handle { m.handle }
        {
            m.handle = null_handle;
        }

        watchpoint& operator=(watchpoint&& m)
        {
            std::swap(handle, m.handle);
            return *this;
        }

        // Set a watchpoint (DPMI 0.9, AX=0B00)
        watchpoint(std::uintptr_t linear_addr, std::size_t size_bytes, watchpoint_type t)
        {
            bool c;
            dpmi::dpmi_error_code error;
            split_uint32_t addr = linear_addr;
            asm volatile
            (
                "int 0x31"
                : "=@ccc" (c)
                , "=a" (error)
                , "=b" (handle)
                : "a" (std::uint16_t { 0x0b00 })
                , "b" (addr.hi)
                , "c" (addr.lo)
                , "d" ((t << 8) | size_bytes)
                : "cc"
            );
            if (c) throw dpmi::dpmi_error { error, __PRETTY_FUNCTION__ };
        }

        // Remove a watchpoint (DPMI 0.9, AX=0B01)
        ~watchpoint()
        {
            if (handle == null_handle) return;
            asm volatile
            (
                "int 0x31"
                :
                : "a" (std::uint16_t { 0x0b01 })
                , "b" (handle)
                : "cc"
            );
            // Ignore errors here. the only possible failure is "invalid
            // handle", which "should never happen".
        }
#else
        constexpr watchpoint(std::uintptr_t, std::size_t, watchpoint_type) { }
#endif

        // Get the current state of this watchpoint (DPMI 0.9, AX=0B02).
        // Returns true if the watchpoint has been triggered.
        bool triggered() const
        {
#ifndef NDEBUG
            bool c;
            union
            {
                dpmi::dpmi_error_code error;
                bool state;
            };
            asm
            (
                "int 0x31"
                : "=@ccc" (c)
                , "=a" (error)
                : "a" (std::uint16_t { 0x0b02 })
                , "b" (handle)
                , "d" (0)
                : "cc"
            );
            if (c) throw dpmi::dpmi_error { error, __PRETTY_FUNCTION__ };
            return state;
#else
            return false;
#endif
        }

        // Reset the state of this watchpoint (DPMI 0.9, AX=0B03)
        void reset()
        {
#ifndef NDEBUG
            bool c;
            dpmi::dpmi_error_code error;
            asm
            (
                "int 0x31;"
                : "=@ccc" (c)
                , "=a" (error)
                : "a" (std::uint16_t { 0x0b03 })
                , "b" (handle)
                : "cc"
            );
            if (c) throw dpmi::dpmi_error { error, __PRETTY_FUNCTION__ };
#endif
        }

    private:
#ifndef NDEBUG
        static constexpr std::uint32_t null_handle { std::numeric_limits<std::uint32_t>::max() };
        std::uint32_t handle { null_handle };
#endif
    };
}
