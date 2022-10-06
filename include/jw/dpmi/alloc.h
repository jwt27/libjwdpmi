/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
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
#include <optional>

#include <jw/common.h>
#include <jw/dpmi/lock.h>
#include <jw/debug/debug.h>
#include <jw/dpmi/irq_mask.h>
#include <jw/dpmi/irq_check.h>
#include <jw/alloc.h>

namespace jw::dpmi
{
    // Custom memory resource which locks all memory it allocates.  This makes
    // STL containers safe to access from interrupt handlers, as long as the
    // handler itself does not allocate anything.  It still relies on
    // _CRT0_FLAG_LOCK_MEMORY to lock code and static data, however.
    struct locking_memory_resource : public std::pmr::memory_resource
    {
        virtual ~locking_memory_resource()
        {
            if (not map) return;
            if (not map->empty()) return;
            map.reset();
        }

    protected:
        [[nodiscard]] virtual void* do_allocate(std::size_t n, std::size_t a) override
        {
            throw_if_irq();
            if (not map) [[unlikely]] map.emplace();
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

        static inline constinit std::optional<std::map<void*, data_lock>> map { std::nullopt };
    };

    // Allocator based on locking_memory_resource
    template <typename T = std::byte>
    struct locking_allocator
    {
        using value_type = T;
        using pointer = T*;

        [[nodiscard]] constexpr T* allocate(std::size_t n)
        {
            return static_cast<T*>(res.allocate(n * sizeof(T), alignof(T)));
        }

        constexpr void deallocate(T* p, std::size_t n)
        {
            res.deallocate(static_cast<void*>(p), n * sizeof(T), alignof(T));
        }

        template <typename U> struct rebind { using other = locking_allocator<U>; };

        template <typename U>
        constexpr locking_allocator(const locking_allocator<U>&) noexcept { }
        constexpr locking_allocator() = default;

        template <typename U> constexpr friend bool operator== (const locking_allocator&, const locking_allocator<U>&) noexcept { return true; }
        template <typename U> constexpr friend bool operator!= (const locking_allocator&, const locking_allocator<U>&) noexcept { return false; }

    private:
        [[no_unique_address]] locking_memory_resource res;
    };

    // Allocates from a pre-allocated locked memory pool.  This allows
    // interrupt handlers to insert/remove elements in STL containers without
    // risking page faults.
    // When specifying a pool size, make sure to account for overhead
    // (reallocation, fragmentation, alignment overhead).
    template<bool lock_self = true>
    struct locked_pool_resource final : public pool_resource, private std::conditional_t<lock_self, class_lock<locked_pool_resource<lock_self>>, empty>
    {
        using base = pool_resource;
        constexpr locked_pool_resource() noexcept : base { &upstream } { }
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

        locking_memory_resource upstream { };
    };

    // Allocator based on locked_pool_resource
    template<bool lock_self = true, typename T = std::byte>
    struct locked_pool_allocator : private std::conditional_t<lock_self, class_lock<locked_pool_allocator<lock_self, T>>, empty>
    {
        using value_type = T;
        using pointer = T*;

        locked_pool_allocator(std::size_t size_bytes)
            : res { std::allocate_shared<locked_pool_resource<false>>(locking_allocator<locked_pool_resource<false>> { }, size_bytes) } { }

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

        std::weak_ptr<locked_pool_resource<false>> resource() noexcept { return res; }

        template <bool lock_other, typename U> friend class locked_pool_allocator;
        template <bool lock_other, typename U> locked_pool_allocator(const locked_pool_allocator<lock_other, U>& c) : res(c.res) { }

        template <typename U> struct rebind { using other = locked_pool_allocator<lock_self, U>; };
        template <bool lock_other, typename U> constexpr friend bool operator== (const locked_pool_allocator& a, const locked_pool_allocator<lock_other, U>& b) noexcept { return a.res == b.res; }
        template <bool lock_other, typename U> constexpr friend bool operator!= (const locked_pool_allocator& a, const locked_pool_allocator<lock_other, U>& b) noexcept { return not (a == b); }

    protected:
        std::shared_ptr<locked_pool_resource<false>> res;
    };
}
