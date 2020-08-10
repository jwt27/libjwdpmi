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
        struct locking_allocator
        {
            using value_type = T;
            using pointer = T*;

            [[nodiscard]] constexpr T* allocate(std::size_t n)
            {
                return static_cast<T*>(memres.allocate(n * sizeof(T), alignof(T)));
            }

            constexpr void deallocate(T* p, std::size_t n)
            {
                memres.deallocate(static_cast<void*>(p), n * sizeof(T), alignof(T));
            }

            template <typename U> struct rebind { using other = locking_allocator<U>; };

            template <typename U>
            constexpr locking_allocator(const locking_allocator<U>&) noexcept { }
            constexpr locking_allocator() = default;

            template <typename U> constexpr friend bool operator== (const locking_allocator&, const locking_allocator<U>&) noexcept { return true; }
            template <typename U> constexpr friend bool operator!= (const locking_allocator&, const locking_allocator<U>&) noexcept { return false; }

        private:
            [[no_unique_address]] locking_memory_resource memres;
        };

        // Allocates from a pre-allocated locked memory pool. This allows interrupt handlers to insert/remove elements in
        // STL containers without risking page faults.
        // When specifying a pool size, make sure to account for overhead (reallocation, fragmentation, alignment overhead).
        template<bool lock_self = true>
        struct locked_pool_resource : public std::pmr::memory_resource, private std::conditional_t<lock_self, class_lock<locked_pool_resource<lock_self>>, empty>
        {
            using pool_type = std::vector<std::byte, locking_allocator<std::byte>>;

            locked_pool_resource(std::size_t size_bytes)
                : pool(std::allocate_shared<pool_type>(locking_allocator<pool_type> { }, size_bytes + sizeof(pool_node), locking_allocator<std::byte> { }))
            {
                new(begin()) pool_node { };
            }

            locked_pool_resource() = delete;
            locked_pool_resource(locked_pool_resource&&) = default;
            locked_pool_resource(const locked_pool_resource&) = default;
            locked_pool_resource& operator=(locked_pool_resource&&) = default;
            locked_pool_resource& operator=(const locked_pool_resource&) = default;

            // Resize the memory pool.  Throws std::bad_alloc if the pool is still in use.
            void resize(std::size_t size_bytes)
            {
                for (auto* i = begin(); i != nullptr; i = i->next)
                {
                    if (not i->free) throw std::bad_alloc { };
                }

                {
                    interrupt_mask no_interrupts_please { };
                    debug::trap_mask dont_trap_here { };
                    pool->clear();
                    pool->resize(size_bytes);
                    new(begin()) pool_node { };
                }
            }

            // Returns maximum number of bytes that can be allocated at once.
            auto max_size() const noexcept
            {
                interrupt_mask no_interrupts_please { };
                debug::trap_mask dont_trap_here { };

                std::size_t n { 0 };
                for (auto* i = begin(); i != nullptr; i = i->next)
                {
                    if (not i->free) continue;

                    n = std::max(n, chunk_size(i));
                }
                return n;
            }

            bool in_pool(void* ptr)
            {
                auto p = reinterpret_cast<std::byte*>(ptr);
                return p > pool->data() && p < (pool->data() + pool->size());
            }

        protected:
            struct pool_node
            {
                pool_node* next { nullptr };
                bool free { true };
                constexpr std::byte* begin() { return reinterpret_cast<std::byte*>(this + 1); }

                constexpr pool_node() noexcept = default;
                constexpr pool_node(pool_node* _next, bool _free) noexcept : next(_next), free(_free) { }
            };

            constexpr pool_node* begin() const noexcept { return static_cast<pool_node*>(aligned_ptr(pool->data(), alignof(pool_node))); }

            // Returns size in bytes.
            std::size_t chunk_size(pool_node* p) const noexcept
            {
                auto end = p->next == nullptr ? pool->data() + pool->size() : reinterpret_cast<std::byte*>(p->next);
                return end - p->begin();
            }

            constexpr void* aligned_ptr(std::byte* p, std::size_t align) const noexcept
            {
                auto a = reinterpret_cast<std::uintptr_t>(p);
                auto b = a & -align;
                if (b != a) b += align;
                return reinterpret_cast<void*>(b);
            }

            [[nodiscard]] virtual void* do_allocate(std::size_t n, std::size_t a) override
            {
                interrupt_mask no_interrupts_please { };
                n += a;

                for (auto* i = begin(); i != nullptr; i = i->next)
                {
                    if (not i->free) continue;

                    if (chunk_size(i) > n + sizeof(pool_node) + alignof(pool_node))         // Split chunk
                    {
                        auto* j = static_cast<pool_node*>(aligned_ptr(i->begin() + n, alignof(pool_node)));
                        j = new(j) pool_node { i->next, true };
                        i = new(i) pool_node { j, false };
                        return aligned_ptr(i->begin(), a);
                    }
                    else if (chunk_size(i) >= n)                                            // Use entire chunk
                    {
                        i->free = false;
                        return aligned_ptr(i->begin(), a);
                    }
                }
                throw std::bad_alloc { };
            }

            virtual void do_deallocate(void* p, std::size_t, std::size_t) noexcept override
            {
                for (pool_node* prev = nullptr, *i = begin(); i != nullptr; prev = i, i = i->next)
                {
                    if (reinterpret_cast<void*>(i->begin()) <= p and reinterpret_cast<void*>(i->next) > p)
                    {
                        i->free = true;
                        if (i->next != nullptr and i->next->free) i->next = i->next->next;
                        if (prev != nullptr and prev->free) prev->next = i->next;
                        return;
                    }
                }
            }

            virtual bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
            {
                auto* o = dynamic_cast<const locked_pool_resource*>(&other);
                return o != nullptr and o->pool == pool;
            }

            locked_pool_resource(const std::shared_ptr<pool_type>& copy) : pool { copy } { }

            std::shared_ptr<pool_type> pool;
        };

        // Legacy allocator based on locked_pool_resource
        template<bool lock_self = true, typename T = byte>
        struct locked_pool_allocator : protected locked_pool_resource<lock_self>
        {
            using base = locked_pool_resource<lock_self>;
            using value_type = T;
            using pointer = T*;

            locked_pool_allocator(std::size_t size_bytes) : base { size_bytes } { }

            locked_pool_allocator() = delete;
            locked_pool_allocator(locked_pool_allocator&&) = default;
            locked_pool_allocator(const locked_pool_allocator&) = default;
            locked_pool_allocator& operator=(locked_pool_allocator&&) = default;
            locked_pool_allocator& operator=(const locked_pool_allocator&) = default;

            [[nodiscard]] T* allocate(std::size_t num_elements)
            {
                return static_cast<T*>(base::allocate(num_elements * sizeof(T), alignof(T)));
            }

            void deallocate(pointer p, std::size_t num_elements)
            {
                base::deallocate(static_cast<void*>(p), num_elements * sizeof(T), alignof(T));
            }

            // Resize the memory pool. Throws std::bad_alloc if the pool is still in use.
            void resize(std::size_t size_bytes)
            {
                base::resize(size_bytes);
            }

            // Returns maximum number of elements that can be allocated at once.
            auto max_size() const noexcept
            {
                auto n = base::max_size();
                return n < alignof(T) ? 0 : (n - alignof(T)) / sizeof(T);
            }

            bool in_pool(T* p)
            {
                return base::in_pool(static_cast<void*>(p));
            }

            template <bool lock_other, typename U> friend class locked_pool_allocator;
            template <bool lock_other, typename U> locked_pool_allocator(const locked_pool_allocator<lock_other, U>& c) : base(c.pool) { }

            template <typename U> struct rebind { using other = locked_pool_allocator<lock_self, U>; };
            template <bool lock_other, typename U> constexpr friend bool operator== (const locked_pool_allocator& a, const locked_pool_allocator<lock_other, U>& b) noexcept { return a.pool == b.pool; }
            template <bool lock_other, typename U> constexpr friend bool operator!= (const locked_pool_allocator& a, const locked_pool_allocator<lock_other, U>& b) noexcept { return !(a == b); }
        };

        static_assert(sizeof(locked_pool_allocator<true>) == sizeof(locked_pool_resource<true>));
    }
}
