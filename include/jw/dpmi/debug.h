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
#include <jw/dpmi/dpmi.h>

namespace jw
{
    namespace dpmi
    {
        // Returns true if in debug mode.
        bool debug();

        // Set a breakpoint
        inline void breakpoint() { if (debug()) asm("int 3"); }

        // Set a watchpoint
        // Remember, only 4 watchpoints can exist simultaneously.
        struct watchpoint
        {
            enum type
            {
                execute,
                read,
                read_write
            };

            template<typename T>
            watchpoint(T* ptr, type t) : watchpoint(memory_info::near_to_linear(ptr), sizeof(T), t) 
            { static_assert(sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1); }

            watchpoint(auto* ptr, type t, std::size_t size) : watchpoint(memory_info::near_to_linear(ptr), size, t) { }

            watchpoint(const watchpoint&) = delete;
            watchpoint& operator=(const watchpoint&) = delete;
            watchpoint(watchpoint&& m) : handle(m.handle) { m.handle = null_handle; }
            watchpoint& operator=(watchpoint&& m) { std::swap(handle, m.handle); return *this; }

            // Set a watchpoint (DPMI 0.9, AX=0B00)
            watchpoint(std::uintptr_t linear_addr, std::size_t size_bytes, type t)
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
                if (c) throw dpmi_error(error, "set_watchpoint");
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

            // Get the current state of this watchpoint. (DPMI 0.9, AX=0B02)
            // Returns true if the watchpoint has been triggered.
            bool get_state() const
            {
                bool c;
                dpmi_error_code error;
                asm("int 0x31;"
                    : "=@ccc" (c)
                    , "=a"(error)
                    : "a"(0x0b02)
                    , "b"(handle)
                    , "d"(0)
                    : "cc");
                if (c) throw dpmi_error(error, "clear_watchpoint");
                return error != 0;
            }

            // Reset the state of this watchpoint (DPMI 0.9, AX=0B03)
            void reset()
            {
                bool c;
                dpmi_error_code error;
                asm (
                    "int 0x31;"
                    : "=@ccc" (c)
                    , "=a"(error)
                    : "a"(0x0b03)
                    , "b"(handle)
                    : "cc");
                if (c) throw dpmi_error(error, "reset_watchpoint");
            }

        private:
            const std::uint32_t null_handle { std::numeric_limits<std::uint32_t>::max() };
            std::uint32_t handle { null_handle };
        };
    }
}
