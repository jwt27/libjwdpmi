/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
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
            struct memory_lock
            {
                memory_lock(const memory_lock& c) = delete;
                memory_lock(memory_lock&& m) noexcept
                    : mem { std::move(m.mem) }, locked { m.locked }
                { m.locked = false; }

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
                    catch (const dpmi_error&) { /* ignore */ }
                }

            protected:
                template<typename T>
                memory_lock(const T* ptr, std::size_t n) : mem { linear_memory::from_pointer(ptr, n) } { lock(); }

                void lock()
                {
                    if (locked) [[unlikely]] return;
                    mem.lock();
                    locked = true;
                }

                void unlock()
                {
                    if (not locked) [[unlikely]] return;
                    mem.unlock();
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
            data_lock(const T* addr, std::size_t n = 1) : memory_lock(addr, n) { }
            using memory_lock::operator=;
        };

        // Locks the memory occupied by a class (in Data Segment) that derives from this.
        template <typename T>
        class class_lock : public detail::memory_lock
        {
            friend T;
            class_lock() : memory_lock(static_cast<const void*>(this), sizeof(T)) { }
            class_lock(class_lock&&) : class_lock() { }
            class_lock(const class_lock&) : class_lock() { }

            class_lock& operator=(const class_lock&) { return *this; }
            class_lock& operator=(class_lock&&) { return *this; }
        };
    }
}
