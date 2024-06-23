/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2016 - 2024 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <new>
#include <memory>
#include <vector>
#include <map>
#include <memory_resource>
#include <optional>

#include <jw/common.h>
#include <jw/dpmi/lock.h>
#include <jw/debug/debug.h>
#include <jw/dpmi/irq_mask.h>
#include <jw/dpmi/irq_check.h>
#include <jw/alloc.h>
#include <jw/main.h>

namespace jw::dpmi
{
    // Custom memory resource which locks all memory it allocates.  This makes
    // STL containers safe to access from interrupt handlers, as long as the
    // handler itself does not allocate anything.  It still relies on
    // _CRT0_FLAG_LOCK_MEMORY to lock code and static data, however.
    inline auto* locking_resource() noexcept
    {
        struct resource final : std::pmr::memory_resource
        {
        protected:
            [[nodiscard]] virtual void* do_allocate(std::size_t n, std::size_t a) override
            {
                throw_if_irq();
                void* p = jw::allocate(n, a);
                linear_memory::from_pointer(p, n).lock();
                return p;
            }

            virtual void do_deallocate(void* p, std::size_t n, std::size_t a) noexcept override
            {
                linear_memory::from_pointer(p, n).unlock();
                jw::free(p, n, a);
            }

            virtual bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
            {
                return this == &other;
            }
        } static memres { };
        return &memres;
    }

    using locking_resource_t = std::remove_cvref_t<decltype(*locking_resource())>;

    // Allocator based on locking_resource
    template <typename T = std::byte>
    struct locking_allocator
    {
        using value_type = T;
        using pointer = T*;

        [[nodiscard]] constexpr T* allocate(std::size_t n)
        {
            return static_cast<T*>(locking_resource()->allocate(n * sizeof(T), alignof(T)));
        }

        constexpr void deallocate(T* p, std::size_t n)
        {
            locking_resource()->deallocate(static_cast<void*>(p), n * sizeof(T), alignof(T));
        }

        template <typename U> struct rebind { using other = locking_allocator<U>; };

        template <typename U>
        constexpr locking_allocator(const locking_allocator<U>&) noexcept { }
        constexpr locking_allocator() = default;

        template <typename U> constexpr friend bool operator== (const locking_allocator&, const locking_allocator<U>&) noexcept { return true; }
        template <typename U> constexpr friend bool operator!= (const locking_allocator&, const locking_allocator<U>&) noexcept { return false; }
    };

    // Allocates from a pre-allocated locked memory pool.  This allows
    // interrupt handlers to insert/remove elements in STL containers without
    // risking page faults.
    // When specifying a pool size, make sure to account for overhead
    // (reallocation, fragmentation, alignment overhead).  And keep in mind
    // that the resource itself must also be allocated in locked memory!
    struct locked_pool_resource final : public pool_resource
    {
        using base = pool_resource;
        locked_pool_resource() noexcept : base { locking_resource() } { }
        locked_pool_resource(std::size_t size_bytes) : locked_pool_resource { } { grow(size_bytes); }

        constexpr locked_pool_resource(locked_pool_resource&& o) noexcept = default;
        constexpr locked_pool_resource& operator=(locked_pool_resource&& o) noexcept = default;
        locked_pool_resource(const locked_pool_resource&) = delete;
        locked_pool_resource& operator=(const locked_pool_resource&) = delete;

        using base::empty;

    private:
        virtual void do_grow(std::size_t bytes) override { grow_alloc<interrupt_mask>(bytes); }
        virtual void do_grow(const std::span<std::byte>& ptr) noexcept override { grow_impl<interrupt_mask>(ptr); }
        virtual void auto_grow(std::size_t) override { throw std::bad_alloc { }; }
        [[nodiscard]] virtual void* do_allocate(std::size_t n, std::size_t a) override { return allocate_impl<interrupt_mask>(n, a); }
    };

    // Allocator based on locked_pool_resource
    template<typename T = std::byte>
    struct locked_pool_allocator
    {
        using value_type = T;
        using pointer = T*;

        locked_pool_allocator(std::size_t size_bytes)
            : res { std::allocate_shared<locked_pool_resource>(locking_allocator<locked_pool_resource> { }, size_bytes) } { }

        locked_pool_allocator() = delete;
        locked_pool_allocator(locked_pool_allocator&&) = default;
        locked_pool_allocator(const locked_pool_allocator&) = default;
        locked_pool_allocator& operator=(locked_pool_allocator&&) = default;
        locked_pool_allocator& operator=(const locked_pool_allocator&) = default;

        [[nodiscard]] T* allocate(std::size_t num_elements)
        {
            return static_cast<T*>(res->allocate(num_elements * sizeof(T), alignof(T)));
        }

        void deallocate(pointer p, std::size_t num_elements)
        {
            res->deallocate(static_cast<void*>(p), num_elements * sizeof(T), alignof(T));
        }

        // Deallocate the memory pool.
        void release() noexcept { res->release(); }

        // Returns true if pool is unallocated.
        bool empty() const noexcept { return res->empty(); }

        // Grow the memory pool by the specified amount.
        void grow(std::size_t size_bytes) { res->grow(size_bytes); }

        // Returns the size of the largest chunk.
        std::size_t max_chunk_size() const noexcept { return res->max_chunk_size(); }

        // Returns maximum number of elements that can be allocated at once.
        std::size_t max_size() const noexcept { return res->max_size(alignof(T)) / sizeof(T); }

        // Returns current pool size in bytes.
        std::size_t size() const noexcept { return res->size(); }

        bool in_pool(T* p) const noexcept { return res->in_pool(static_cast<void*>(p)); }

        std::weak_ptr<locked_pool_resource> resource() noexcept { return res; }

        template <typename U> friend class locked_pool_allocator;
        template <typename U> locked_pool_allocator(const locked_pool_allocator<U>& c) : res(c.res) { }

        template <typename U> struct rebind { using other = locked_pool_allocator<U>; };
        template <typename U> constexpr friend bool operator== (const locked_pool_allocator& a, const locked_pool_allocator<U>& b) noexcept { return a.res == b.res; }
        template <typename U> constexpr friend bool operator!= (const locked_pool_allocator& a, const locked_pool_allocator<U>& b) noexcept { return not (a == b); }

    protected:
        std::shared_ptr<locked_pool_resource> res;
    };

    // Returns a std::pmr::memory_resource that allocates from the global
    // locked pool, same as 'operator new (jw::locked)'.
    inline auto* global_locked_pool_resource() noexcept
    {
        struct resource final : std::pmr::memory_resource
        {
        protected:
            [[nodiscard]] virtual void* do_allocate(std::size_t n, std::size_t a) override
            {
                return jw::allocate_locked(n, a);
            }

            virtual void do_deallocate(void* p, std::size_t n, std::size_t a) noexcept override
            {
                jw::free_locked(p, n, a);
            }

            virtual bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
            {
                return this == &other;
            }
        } static memres { };
        return &memres;
    }

    using global_locked_pool_resource_t = std::remove_cvref_t<decltype(*global_locked_pool_resource())>;

    // Allocator based on global_locked_pool_resource
    template <typename T = std::byte>
    struct global_locked_pool_allocator
    {
        using value_type = T;
        using pointer = T*;

        [[nodiscard]] constexpr T* allocate(std::size_t n)
        {
            return static_cast<T*>(global_locked_pool_resource()->allocate(n * sizeof(T), alignof(T)));
        }

        constexpr void deallocate(T* p, std::size_t n)
        {
            global_locked_pool_resource()->deallocate(static_cast<void*>(p), n * sizeof(T), alignof(T));
        }

        template <typename U> struct rebind { using other = global_locked_pool_allocator<U>; };

        template <typename U>
        constexpr global_locked_pool_allocator(const global_locked_pool_allocator<U>&) noexcept { }
        constexpr global_locked_pool_allocator() = default;

        template <typename U> constexpr friend bool operator== (const global_locked_pool_allocator&, const global_locked_pool_allocator<U>&) noexcept { return true; }
        template <typename U> constexpr friend bool operator!= (const global_locked_pool_allocator&, const global_locked_pool_allocator<U>&) noexcept { return false; }
    };
}
