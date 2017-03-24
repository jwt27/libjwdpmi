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

// Provides easy means to lock classes and other data in memory. This prevents 
// things from being swapped out, thus avoiding page faults.

#pragma once
#include <jw/dpmi/memory.h>

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            class memory_lock
            {
            public:
                memory_lock(const memory_lock& c) = delete;
                memory_lock(memory_lock&& m) noexcept : mem(std::move(m.mem)) { m.locked = false; }

                memory_lock& operator=(const memory_lock& c) = delete;
                memory_lock& operator=(memory_lock&& m) noexcept
                {
                    std::swap(mem, m.mem);
                    std::swap(locked, m.locked);
                    return *this;
                };

                virtual ~memory_lock()
                {
                    try { unlock(); }
                    catch (const std::exception& e)
                    {
                        std::cerr << "Caught exception in memory_lock destructor! \n"
                            << "locked region: " << std::hex << mem.get_linear_address() << " - " << (mem.get_linear_address() + mem.get_size()) << "\n"
                            << "error: " << e.what() << std::endl;
                    }
                    catch (...) { std::cerr << "Caught exception in memory_lock destructor!" << std::endl; }
                }

            protected:
                template<typename T>
                memory_lock(selector segment, const T* ptr, std::size_t size_bytes) : mem(segment, ptr, size_bytes) { lock(); }

                void lock()
                {
                    if (locked) return;
                    mem.lock_memory();
                    locked = true;
                }

                void unlock()
                {
                    if (!locked) return;
                    mem.unlock_memory();
                    locked = false;
                }

                memory mem;
                bool locked { false };
            };
        }

        // Locks the memory occupied by one or more objects (in Data Segment)
        struct data_lock final : protected detail::memory_lock
        {
            template<typename T>
            data_lock(const T* addr, std::size_t num_elements = 1) : memory_lock(get_ds(), reinterpret_cast<void*>(addr), num_elements * sizeof(T)) { }
            data_lock(const void* addr, std::size_t size_bytes) : memory_lock(get_ds(), addr, size_bytes) { }
            using memory_lock::operator=;
        };

        // Locks the memory occupied by a class (in Data Segment) that derives from this.
        template <typename T>
        struct class_lock : public detail::memory_lock
        {
            class_lock() : memory_lock(get_ds(), this, sizeof(T)) { }
            class_lock(class_lock&&) : class_lock() { }
            class_lock(const class_lock&) : class_lock() { }

            class_lock& operator=(const class_lock&) { return *this; }
            class_lock& operator=(class_lock&&) { return *this; }
        };
    }
}
