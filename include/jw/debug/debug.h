/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <limits>
#include <experimental/source_location>
#include <jw/dpmi/memory.h>

namespace jw
{
    namespace debug
    {
        namespace detail
        {
#           ifndef NDEBUG
            extern bool debug_mode;
            extern volatile int current_signal;
#           endif
        }

#       ifndef NDEBUG
        // Returns true if in debug mode.
        inline bool debug() noexcept { return detail::debug_mode; }
#       else
        constexpr inline bool debug() noexcept { return false; }
#       endif

        // Set a breakpoint
        inline void breakpoint() { if (debug()) asm("int 3"); }

        // Set a breakpoint with specified signal.
        // signal can be an exception number, C signal number, or any user-defined signal.
        inline void break_with_signal([[maybe_unused]] int signal)
        {
#           ifndef NDEBUG
            detail::current_signal = signal;
            breakpoint();
#           endif
        }

        // Unwind stack and print backtrace.
        void print_backtrace() noexcept;

        struct assertion_failed : public std::runtime_error
        {
            using runtime_error::runtime_error;
        };

#       ifndef NDEBUG
        [[gnu::noinline]]
        inline void throw_assert(bool condition, std::experimental::source_location loc = std::experimental::source_location::current())
        {
            if (not condition)
            {
                std::stringstream s { };
                s << "Assertion failed at 0x" << std::hex << __builtin_return_address(0) << " in function " << loc.function_name();
                s << " (" << loc.file_name() << ':' << std::dec << loc.line() << ')';
                breakpoint();
                throw assertion_failed { s.str() };
            }
        }
#       else
        void throw_assert(bool) { }
#       endif

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
            bool failed { false };
        #else
            constexpr trap_mask() noexcept { }
        #endif
        };

        // Set a watchpoint
        // Remember, only 4 watchpoints can exist simultaneously.
        struct watchpoint
        {
            enum watchpoint_type
            {
                execute,
                write,
                read_write
            };

            template<typename T>
            watchpoint(T* ptr, watchpoint_type t) : watchpoint(dpmi::near_to_linear(ptr), sizeof(T), t)
            {
                static_assert(sizeof(T) == 4 or sizeof(T) == 2 or sizeof(T) == 1);
            }

            watchpoint(auto* ptr, watchpoint_type t, std::size_t size) : watchpoint(dpmi::near_to_linear(ptr), size, t) { }

        #ifndef NDEBUG
            watchpoint(const watchpoint&) = delete;
            watchpoint& operator=(const watchpoint&) = delete;
            watchpoint(watchpoint&& m) : handle(m.handle), type(m.type) { m.handle = null_handle; }
            watchpoint& operator=(watchpoint&& m) 
            { 
                std::swap(handle, m.handle); 
                type = m.type; 
                return *this; 
            }

            // Set a watchpoint (DPMI 0.9, AX=0B00)
            watchpoint(std::uintptr_t linear_addr, std::size_t size_bytes, watchpoint_type t) : type(t)
            {
                bool c;
                dpmi::dpmi_error_code error;
                split_uint32_t addr = linear_addr;
                asm volatile(
                    "int 0x31;"
                    : "=@ccc"(c)
                    , "=a"(error)
                    , "=b"(handle)
                    : "a"(0x0b00)
                    , "b"(addr.hi)
                    , "c"(addr.lo)
                    , "d"((t << 8) | size_bytes)
                    : "cc");
                if (c) throw dpmi::dpmi_error(error, __PRETTY_FUNCTION__);
            }

            // Remove a watchpoint (DPMI 0.9, AX=0B01)
            ~watchpoint()
            {
                if (handle == null_handle) return;
                asm volatile(
                    "int 0x31;"
                    ::"a"(0x0b01)
                    , "b"(handle)
                    : "cc");
                // disregard errors here. the only possible failure is invalid handle, which "should never happen"
            }
        #else
            constexpr watchpoint(std::uintptr_t, std::size_t, watchpoint_type t) : type(t) { }
        #endif

            // Get the current state of this watchpoint. (DPMI 0.9, AX=0B02)
            // Returns true if the watchpoint has been triggered.
            bool get_state() const
            {
            #ifndef NDEBUG
                bool c;
                dpmi::dpmi_error_code error;
                asm("int 0x31;"
                    : "=@ccc" (c)
                    , "=a"(error)
                    : "a"(0x0b02)
                    , "b"(handle)
                    , "d"(0)
                    : "cc");
                if (c) throw dpmi::dpmi_error(error, __PRETTY_FUNCTION__);
                return error != 0;
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
                asm (
                    "int 0x31;"
                    : "=@ccc" (c)
                    , "=a"(error)
                    : "a"(0x0b03)
                    , "b"(handle)
                    : "cc");
                if (c) throw dpmi::dpmi_error(error, __PRETTY_FUNCTION__);
            #endif
            }

            auto get_type() { return type; }

        private:
        #ifndef NDEBUG
            static constexpr std::uint32_t null_handle { std::numeric_limits<std::uint32_t>::max() };
            std::uint32_t handle { null_handle };
        #endif
            watchpoint_type type;
        };
    }
}
