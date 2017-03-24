/******************************* libjwdpmi **********************************
Copyright (C) 2016-2017  J.W. Jagersma

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once
#include <limits>
#include <jw/dpmi/memory.h>

namespace jw
{
    namespace dpmi
    {
        // Returns true if in debug mode.
    #ifndef NDEBUG
        bool debug() noexcept;
    #else
        constexpr bool debug() noexcept { return false; }
    #endif

        // Set a breakpoint
        inline void breakpoint() { if (debug()) asm("int 3"); }

        // Disable the trap flag
        struct trap_mask
        {
        #ifndef NDEBUG
            trap_mask() noexcept;
            [[gnu::optimize("no-omit-frame-pointer")]] ~trap_mask() noexcept;

            trap_mask(const trap_mask&) = delete;
            trap_mask(trap_mask&&) = delete;
            trap_mask& operator=(const trap_mask&) = delete;
            trap_mask& operator=(trap_mask&&) = delete;
        private:
            bool fail { false };
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
                read,
                read_write
            };

            template<typename T>
            watchpoint(T* ptr, watchpoint_type t) : watchpoint(near_to_linear(ptr), sizeof(T), t) 
            { static_assert(sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1); }

            watchpoint(auto* ptr, watchpoint_type t, std::size_t size) : watchpoint(near_to_linear(ptr), size, t) { }

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
                dpmi_error_code error;
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
                if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
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
                dpmi_error_code error;
                asm("int 0x31;"
                    : "=@ccc" (c)
                    , "=a"(error)
                    : "a"(0x0b02)
                    , "b"(handle)
                    , "d"(0)
                    : "cc");
                if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
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
                dpmi_error_code error;
                asm (
                    "int 0x31;"
                    : "=@ccc" (c)
                    , "=a"(error)
                    : "a"(0x0b03)
                    , "b"(handle)
                    : "cc");
                if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
            #endif
            }

            auto get_type() { return type; }

        private:
        #ifndef NDEBUG
            const std::uint32_t null_handle { std::numeric_limits<std::uint32_t>::max() };
            std::uint32_t handle { null_handle };
        #endif
            watchpoint_type type;
        };
    }
}
