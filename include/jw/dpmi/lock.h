/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

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
                            << "locked region: " << std::hex << mem.get_address() << " - " << (mem.get_address() + mem.get_size()) << "\n"
                            << "error: " << e.what() << std::endl;
                    }
                    catch (...) { std::cerr << "Caught exception in memory_lock destructor!" << std::endl; }
                }

            protected:
                template<typename T>
                memory_lock(selector segment, const T* ptr, std::size_t size_bytes) : mem(segment, ptr, size_bytes) { lock(); }

                void lock()
                {
                    if (locked) [[unlikely]] return;
                    mem.lock_memory();
                    locked = true;
                }

                void unlock()
                {
                    if (not locked) [[unlikely]] return;
                    mem.unlock_memory();
                    locked = false;
                }

                linear_memory mem;
                bool locked { false };
            };
        }

        // Locks the memory occupied by one or more objects (in Data Segment)
        struct data_lock final : protected detail::memory_lock
        {
            template<typename T>
            data_lock(const T* addr, std::size_t num_elements = 1) : memory_lock(get_ds(), reinterpret_cast<const void*>(addr), num_elements * sizeof(T)) { }
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
