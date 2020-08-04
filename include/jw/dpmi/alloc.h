/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <new>
#include <memory>
#include <vector>
#include <map>
#include <memory_resource>

#include <jw/common.h>
#include <jw/dpmi/lock.h>
#include <jw/debug/debug.h>
#include <jw/dpmi/irq_mask.h>
#include <jw/dpmi/irq_check.h>

namespace jw
{
    namespace dpmi
    {
        // Custom memory resource which locks all memory it allocates. This makes STL containers safe to
        // access from interrupt handlers, as long as the handler itself does not allocate anything.
        // It still relies on _CRT0_FLAG_LOCK_MEMORY to lock code and static data, however.
        struct locking_memory_resource : public std::pmr::memory_resource
        {
            virtual ~locking_memory_resource()
            {
                if (map == nullptr) return;
                if (not map->empty()) return;
                delete map;
                map = nullptr;
            }

        protected:
            [[nodiscard]] virtual void* do_allocate(std::size_t n, std::size_t a) override
            {
                throw_if_irq();
                if (map == nullptr) [[unlikely]] map = new std::map<void*, data_lock> { };
                void* p = ::operator new(n, std::align_val_t { a });
                map->emplace(p, data_lock { p, n });
                return p;
            }

            virtual void do_deallocate(void* p, std::size_t, std::size_t) noexcept override
            {
                map->erase(p);
                ::operator delete(p);
            }

            virtual bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
            {
                auto* o = dynamic_cast<const locking_memory_resource*>(&other);
                return (o != nullptr);
            }

            static inline std::map<void*, data_lock>* map { };
        };

        // Legacy allocator based on locking_memory_resource
        template <typename T = byte>
        struct locking_allocator : protected locking_memory_resource
        {
            using value_type = T;
            using pointer = T*;

            [[nodiscard]] constexpr T* allocate(std::size_t n)
            {
                return static_cast<T*>(locking_memory_resource::allocate(n * sizeof(T), alignof(T)));
            }

            constexpr void deallocate(T* p, std::size_t n)
            {
                locking_memory_resource::deallocate(static_cast<void*>(p), n);
            }

            template <typename U> struct rebind { using other = locking_allocator<U>; };

            template <typename U>
            constexpr locking_allocator(const locking_allocator<U>&) noexcept { }
            constexpr locking_allocator() { };

            template <typename U> constexpr friend bool operator== (const locking_allocator&, const locking_allocator<U>&) noexcept { return true; }
            template <typename U> constexpr friend bool operator!= (const locking_allocator& a, const locking_allocator<U>& b) noexcept { return !(a == b); }
        };

        struct empty { };

        // Allocates from a pre-allocated locked memory pool. This allows interrupt handlers to insert/remove elements in
        // STL containers without risking page faults.
        // When specifying a pool size, make sure to account for overhead (reallocation, fragmentation, alignment overhead).
        // Keep in mind each allocation takes at least sizeof(T) + alignof(T) + sizeof(pool_node) bytes. Therefore this
        // allocator is rather space-inefficient for single-element allocations.
        template<bool lock_self = true, typename T = byte>
        struct locked_pool_allocator : std::conditional_t<lock_self, class_lock<locked_pool_allocator<lock_self, T>>, empty>
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

            [[nodiscard]] T* allocate(std::size_t num_elements)
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

            void deallocate(pointer p, std::size_t)
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
                throw std::bad_alloc { };
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
                    debug::trap_mask dont_trap_here { };
                    pool->clear();
                    pool->resize(size_bytes);
                    new(begin()) pool_node { };
                }
            }

            // Returns maximum number of elements that can be allocated at once.
            auto max_size() const noexcept
            {
                interrupt_mask no_interrupts_please { };
                debug::trap_mask dont_trap_here { };

                std::size_t n { 0 };
                for (auto* i = begin(); i != nullptr; i = i->next)
                {
                    if (!i->free) continue;
                    while (i->next != nullptr && i->next->free) i->next = i->next->next;

                    n = std::max(n, chunk_size(i));
                }
                return n < alignof(T) ? 0 : (n - alignof(T)) / sizeof(T);
            }

            bool in_pool(auto* ptr)
            {
                auto p = reinterpret_cast<byte*>(ptr);
                return p > pool->data() && p < (pool->data() + pool->size());
            }

            locked_pool_allocator() = delete;
            locked_pool_allocator(locked_pool_allocator&&) = default;
            locked_pool_allocator(const locked_pool_allocator&) = default;
            locked_pool_allocator& operator=(const locked_pool_allocator&) = default;

            locked_pool_allocator(std::size_t size_bytes)
                : pool(std::allocate_shared<pool_type>(locking_allocator<> { }, size_bytes + sizeof(pool_node), locking_allocator<> { }))
            {
                new(begin()) pool_node { };
            }

            template <bool lock_other, typename U> friend class locked_pool_allocator;
            template <bool lock_other, typename U> locked_pool_allocator(const locked_pool_allocator<lock_other, U>& c) : pool(c.pool) { }

            template <typename U> struct rebind { using other = locked_pool_allocator<lock_self, U>; };
            template <bool lock_other, typename U> constexpr friend bool operator== (const locked_pool_allocator& a, const locked_pool_allocator<lock_other, U>& b) noexcept { return a.pool == b.pool; }
            template <bool lock_other, typename U> constexpr friend bool operator!= (const locked_pool_allocator& a, const locked_pool_allocator<lock_other, U>& b) noexcept { return !(a == b); }

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

        template <bool lock_self = true>
        struct locked_pool_memory_resource : protected locked_pool_allocator<lock_self, byte>, public std::pmr::memory_resource
        {
            friend struct locked_pool_memory_resource<not lock_self>;
            using base = locked_pool_allocator<lock_self, byte>;

            locked_pool_memory_resource(std::size_t size_bytes) : base(size_bytes) { }

        protected:
            [[nodiscard]] virtual void* do_allocate(std::size_t n, std::size_t) override
            {
                return reinterpret_cast<void*>(base::allocate(n));
            }

            virtual void do_deallocate(void* ap, std::size_t size, std::size_t) noexcept override
            {
                base::deallocate(reinterpret_cast<byte*>(ap), size);
            }

            virtual bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
            {
                return dynamic_cast<const locked_pool_memory_resource*>(&other)->pool == this->pool
                    or dynamic_cast<const locked_pool_memory_resource<not lock_self>*>(&other)->pool == this->pool;
            }
        };
    }
}
