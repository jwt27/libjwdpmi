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

// Custom allocators for use in STL containers.

#pragma once
#include <memory>
#include <vector>
#include <map>

#include <jw/typedef.h>
#include <jw/dpmi/lock.h>
#include <jw/dpmi/irq_mask.h>
#include <jw/dpmi/irq_check.h>

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            class locking_allocator_base
            {
            protected:
                static std::map<void*, data_lock>* map;
            };
        }

        // Custom allocator which locks all memory it allocates. This makes STL containers safe to
        // access from interrupt handlers, as long as the handler itself does not allocate anything.
        // It still relies on _CRT0_FLAG_LOCK_MEMORY to lock code and static data, however.
        template <typename T = byte>
        class locking_allocator : public detail::locking_allocator_base
        {
        public:
            using value_type = T;
            using pointer = T*;

            auto allocate(std::size_t n)
            {
                throw_if_irq();
                n *= sizeof(T);
                auto* p = ::operator new(n);
                map->emplace(p, data_lock { p, n });
                return static_cast<pointer>(p);
            }

            void deallocate(pointer p, std::size_t)
            {
                map->erase(p);
                ::operator delete(p);
            }

            std::size_t max_size() const noexcept 
            { 
                if (in_irq_context()) return 0;
                return std::allocator<T>{ }.max_size(); 
            }

            template <typename U> using rebind = locking_allocator<U>;
            using other = locking_allocator;

            template <typename U>
            constexpr locking_allocator(const locking_allocator<U>&) noexcept { }
            locking_allocator() { if (map == nullptr) map = new std::map<void*, data_lock> { }; };
            ~locking_allocator()
            {
                if (map == nullptr) return;
                if (!map->empty()) return;
                delete map;
                map = nullptr;
            }

            template <typename U> constexpr friend bool operator == (const locking_allocator&, const locking_allocator<U>&) noexcept { return true; }
            template <typename U> constexpr friend bool operator != (const locking_allocator& a, const locking_allocator<U>& b) noexcept { return !(a == b); }
        };


        // Allocates from a pre-allocated locked memory pool. This allows interrupt handlers to insert/remove elements in 
        // STL containers without risking page faults.
        // When specifying a pool size, make sure to account for overhead (reallocation, fragmentation, alignment overhead). 
        // Keep in mind each allocation takes at least sizeof(T) + alignof(T) + sizeof(pool_node) bytes. Therefore this 
        // allocator is rather space-inefficient for single-element allocations. 
        template<typename T = byte>
        struct locked_pool_allocator : class_lock<locked_pool_allocator<T>>
        {
            using pool_type = std::vector<byte, locking_allocator<>>;
            using value_type = T;
            using pointer = T*;

            struct pool_node
            {
                pool_node* next { nullptr };
                bool free { true };
                constexpr byte* begin() { return reinterpret_cast<byte*>(this + 1); }

                constexpr pool_node() noexcept = default;
                constexpr pool_node(pool_node* _next, bool _free) noexcept :next(_next), free(_free) { }
            };

            auto allocate(std::size_t num_elements)
            {
                interrupt_mask no_interrupts_please { };

                auto n = num_elements * sizeof(T) + alignof(T);
                for (auto* i = begin(); i != nullptr; i = i->next)
                {
                    if (!i->free) continue;

                    while (i->next != nullptr && i->next->free) i->next = i->next->next;    // Merge contiguous free chunks

                    if (chunk_size(i) > n + sizeof(pool_node) + alignof(pool_node))         // Split chunk
                    {
                        auto* j = aligned_ptr<pool_node>(i->begin() + n);
                        j = new(j) pool_node { i->next, true };
                        i = new(i) pool_node { j, false };
                        return aligned_ptr<T>(i->begin());
                    }
                    else if (chunk_size(i) >= n)                                            // Use entire chunk
                    {
                        i->free = false;
                        return aligned_ptr<T>(i->begin());
                    }
                }
                throw std::bad_alloc { };
            }

            void deallocate(pointer p, std::size_t) noexcept
            {
                //interrupt_mask no_interrupts_please { };

                for (auto i = begin(); i != nullptr; i = i->next) // TODO: this might be slow
                {
                    //if (reinterpret_cast<pointer>(i->begin()) <= p && reinterpret_cast<pointer>(i->next) > p)
                    if (aligned_ptr<T>(i->begin()) == p)
                    {
                        i->free = true;
                        return;
                    }
                }
                //throw std::bad_alloc { }; // TODO: log error and do something sensible.
            }

            // Resize the memory pool. Throws std::bad_alloc if the pool is still in use.
            void resize(std::size_t size_bytes)
            {
                for (auto* i = begin(); i != nullptr; i = i->next)
                {
                    if (!i->free) throw std::bad_alloc { };
                }

                {
                    interrupt_mask no_interrupts_please { };
                    pool->resize(size_bytes);
                }
            }

            // Returns maximum number of elements that can be allocated at once.
            auto max_size() const noexcept
            {
                interrupt_mask no_interrupts_please { };

                std::size_t n { 0 };
                for (auto* i = begin(); i != nullptr; i = i->next)
                {
                    if (!i->free) continue;
                    while (i->next != nullptr && i->next->free) i->next = i->next->next;

                    n = std::max(n, chunk_size(i));
                }
                return n < alignof(T) ? 0 : (n - alignof(T)) / sizeof(T);
            }

            locked_pool_allocator() = delete;
            locked_pool_allocator(locked_pool_allocator&&) = default;
            locked_pool_allocator(const locked_pool_allocator&) = default;
            locked_pool_allocator& operator=(const locked_pool_allocator&) = default;

            locked_pool_allocator(std::size_t size_bytes = { })
                : pool(std::allocate_shared<pool_type>(locking_allocator<> { }, size_bytes + sizeof(pool_node), locking_allocator<> { }))
            {
                new(begin()) pool_node { };
            }

            template <typename U> friend class locked_pool_allocator;
            template <typename U> locked_pool_allocator(const locked_pool_allocator<U>& c) : pool(c.pool) { }

            template <typename U> struct rebind { using other = locked_pool_allocator<U>; };
            template <typename U> constexpr friend bool operator== (const locked_pool_allocator& a, const locked_pool_allocator<U>& b) noexcept { return a.pool == b.pool; }
            template <typename U> constexpr friend bool operator!= (const locked_pool_allocator& a, const locked_pool_allocator<U>& b) noexcept { return !(a == b); }

        protected:
            constexpr auto* begin() const noexcept { return aligned_ptr<pool_node>(pool->data()); }

            // Returns size in bytes.
            std::size_t chunk_size(pool_node* p) const noexcept
            {
                auto end = p->next == nullptr ? pool->data() + pool->size() : reinterpret_cast<byte*>(p->next);
                return end - p->begin();
            }

            // Align pointer to alignof(U), rounding upwards.
            template<typename U>
            constexpr auto* aligned_ptr(byte* p) const noexcept
            {
                auto a = reinterpret_cast<std::uintptr_t>(p);
                auto b = a & -alignof(U);
                if (b != a) b += alignof(U);
                return reinterpret_cast<U*>(b);
            }

            std::shared_ptr<pool_type> pool;
        };
    }
}
