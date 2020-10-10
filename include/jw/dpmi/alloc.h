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
                return o != nullptr;
            }

            static inline std::map<void*, data_lock>* map { };
        };

        // Legacy allocator based on locking_memory_resource
        template <typename T = std::byte>
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
            locked_pool_resource(std::size_t size_bytes)
                : pool { size_bytes > 0 ? memres.allocate(size_bytes, alignof(pool_node)) : nullptr }
                , pool_size { size_bytes }
                , first_free { size_bytes >= sizeof(pool_node) ? begin() : nullptr }
            {
                if (pool_size > sizeof(pool_node))
                    new(begin()) pool_node { };
            }

            virtual ~locked_pool_resource() noexcept
            {
                release();
            }

            constexpr locked_pool_resource(locked_pool_resource&& o) noexcept
                : pool { o.pool }, pool_size { o.pool_size }, first_free { o.first_free }
            {
                o.reset();
            }

            constexpr locked_pool_resource& operator=(locked_pool_resource&& o) noexcept
            {
                using std::swap;
                swap(pool, o.pool);
                swap(pool_size, o.pool_size);
                swap(first_free, o.first_free);
            }

            locked_pool_resource() = delete;
            locked_pool_resource(const locked_pool_resource&) = delete;
            locked_pool_resource& operator=(const locked_pool_resource&) = delete;

            constexpr std::size_t size() const noexcept
            {
                return pool_size;
            }

            // Deallocate the memory pool
            void release() noexcept
            {
                if (pool != nullptr) memres.deallocate(pool, pool_size);
                reset();
            }

            // Returns true if pool is unallocated
            bool empty() const noexcept
            {
                auto first = begin();
                return pool == nullptr or (first->free and first->next == nullptr);
            }

            // Resize the memory pool.  Throws std::bad_alloc if the pool is still in use.
            void resize(std::size_t size_bytes)
            {
                if (not empty()) throw std::bad_alloc { };
                interrupt_mask no_interrupts_please { };
                debug::trap_mask dont_trap_here { };
                try
                {
                    release();
                    new (this) locked_pool_resource { size_bytes };
                }
                catch (...)
                {
                    reset();
                    throw;
                }
            }

            // Returns maximum number of bytes that can be allocated at once.
            auto max_size() const noexcept
            {
                interrupt_mask no_interrupts_please { };
                debug::trap_mask dont_trap_here { };
                std::size_t n { 0 };
                for (auto* i = first_free; i != nullptr; i = i->next)
                {
                    if (not i->free) continue;
                    n = std::max(n, chunk_size(i));
                }
                return n;
            }

            bool in_pool(void* ptr) const noexcept
            {
                auto p = reinterpret_cast<const std::byte*>(ptr);
                return p > data() and p < (data() + size());
            }

        protected:
            struct pool_node
            {
                pool_node* next { nullptr };
                bool free { true };
                constexpr auto* begin() noexcept { return reinterpret_cast<std::byte*>(this + 1); }
                constexpr auto* begin() const noexcept { return reinterpret_cast<const std::byte*>(this + 1); }

                constexpr pool_node() noexcept = default;
                constexpr pool_node(pool_node* _next, bool _free) noexcept : next(_next), free(_free) { }
            };

            auto* begin() noexcept { return static_cast<pool_node*>(pool); }
            auto* data() noexcept { return static_cast<std::byte*>(pool); }
            auto* begin() const noexcept { return static_cast<const pool_node*>(pool); }
            auto* data() const noexcept { return static_cast<const std::byte*>(pool); }

            constexpr void reset() noexcept
            {
                pool = nullptr;
                pool_size = 0;
                first_free = nullptr;
            }

            static constexpr void* aligned_ptr(void* p, std::size_t align) noexcept
            {
                auto a = reinterpret_cast<std::uintptr_t>(p);
                auto b = a & -align;
                if (b != a) b += align;
                return reinterpret_cast<void*>(b);
            }

            std::size_t chunk_size(const pool_node* p) const noexcept
            {
                auto* end = p->next == nullptr ? data() + size() : reinterpret_cast<std::byte*>(p->next);
                return end - p->begin();
            }

            [[nodiscard]] virtual void* do_allocate(std::size_t n, std::size_t a) override
            {
                interrupt_mask no_interrupts_please { };
                if (first_free == nullptr) throw std::bad_alloc { };
                n += a;
                for (auto* i = first_free; i != nullptr; i = i->next)
                {
                    if (not i->free) continue;

                    if (chunk_size(i) > n + sizeof(pool_node) + alignof(pool_node)) // Split chunk
                    {
                        auto* j = static_cast<pool_node*>(aligned_ptr(i->begin() + n, alignof(pool_node)));
                        j = new(j) pool_node { i->next, true };
                        i = new(i) pool_node { j, false };
                        if (i == first_free) first_free = j;
                        return aligned_ptr(i->begin(), a);
                    }
                    else if (chunk_size(i) >= n)                                    // Use entire chunk
                    {
                        i->free = false;
                        if (i == first_free) first_free = i->next;
                        return aligned_ptr(i->begin(), a);
                    }
                }
                throw std::bad_alloc { };
            }

            virtual void do_deallocate(void* p, std::size_t, std::size_t) noexcept override
            {
                for (pool_node* prev = nullptr, *i = begin(); i != nullptr; prev = i, i = i->next)
                {
                    if (reinterpret_cast<void*>(i->next) > p and reinterpret_cast<void*>(i->begin()) <= p)
                    {
                        interrupt_mask no_interrupts_please { };
                        i->free = true;
                        if (i < first_free or first_free == nullptr) first_free = i;
                        if (i->next != nullptr and i->next->free) i->next = i->next->next;
                        if (prev != nullptr and prev->free) prev->next = i->next;
                        return;
                    }
                }
            }

            virtual bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
            {
                return &other == static_cast<const std::pmr::memory_resource*>(this);
            }

            [[no_unique_address]] locking_memory_resource memres { };
            void* pool;
            std::size_t pool_size;
            pool_node* first_free;
        };

        // Legacy allocator based on locked_pool_resource
        template<bool lock_self = true, typename T = byte>
        struct locked_pool_allocator : private std::conditional_t<lock_self, class_lock<locked_pool_allocator<lock_self, T>>, empty>
        {
            using value_type = T;
            using pointer = T*;

            locked_pool_allocator(std::size_t size_bytes)
                : memres { std::allocate_shared<locked_pool_resource<false>>(locking_allocator<locked_pool_resource<false>> { }, size_bytes) } { }

            locked_pool_allocator() = delete;
            locked_pool_allocator(locked_pool_allocator&&) = default;
            locked_pool_allocator(const locked_pool_allocator&) = default;
            locked_pool_allocator& operator=(locked_pool_allocator&&) = default;
            locked_pool_allocator& operator=(const locked_pool_allocator&) = default;

            [[nodiscard]] T* allocate(std::size_t num_elements)
            {
                return static_cast<T*>(memres->allocate(num_elements * sizeof(T), alignof(T)));
            }

            void deallocate(pointer p, std::size_t num_elements)
            {
                memres->deallocate(static_cast<void*>(p), num_elements * sizeof(T), alignof(T));
            }

            // Deallocate the memory pool
            void release() noexcept
            {
                memres->release();
            }

            // Returns true if pool is unallocated
            bool empty() const noexcept
            {
                return memres->empty();
            }

            // Resize the memory pool. Throws std::bad_alloc if the pool is still in use.
            void resize(std::size_t size_bytes)
            {
                memres->resize(size_bytes);
            }

            // Returns maximum number of elements that can be allocated at once.
            std::size_t max_size() const noexcept
            {
                auto n = memres->max_size();
                return n < alignof(T) ? 0 : (n - alignof(T)) / sizeof(T);
            }

            // Returns current pool size in bytes.
            std::size_t size() const noexcept
            {
                return memres->size();
            }

            bool in_pool(T* p) const noexcept
            {
                return memres->in_pool(static_cast<void*>(p));
            }

            std::weak_ptr<locked_pool_resource<false>> resource() noexcept
            {
                return memres;
            }

            template <bool lock_other, typename U> friend class locked_pool_allocator;
            template <bool lock_other, typename U> locked_pool_allocator(const locked_pool_allocator<lock_other, U>& c) : memres(c.memres) { }

            template <typename U> struct rebind { using other = locked_pool_allocator<lock_self, U>; };
            template <bool lock_other, typename U> constexpr friend bool operator== (const locked_pool_allocator& a, const locked_pool_allocator<lock_other, U>& b) noexcept { return a.pool == b.pool; }
            template <bool lock_other, typename U> constexpr friend bool operator!= (const locked_pool_allocator& a, const locked_pool_allocator<lock_other, U>& b) noexcept { return !(a == b); }

        protected:
            std::shared_ptr<locked_pool_resource<false>> memres;
        };
    }
}
