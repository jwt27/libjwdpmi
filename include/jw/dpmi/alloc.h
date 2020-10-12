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
                : pool_size { size_bytes }
                , pool { size_bytes > 0 ? memres.allocate(size_bytes, alignof(pool_node)) : nullptr }
                , root { size_bytes >= sizeof(pool_node) ? static_cast<pool_node*>(pool) : nullptr }
            {
                if (pool_size > sizeof(pool_node))
                    new(pool) pool_node { pool_size };
            }

            virtual ~locked_pool_resource() noexcept
            {
                release();
            }

            constexpr locked_pool_resource(locked_pool_resource&& o) noexcept
                : num_allocs { o.num_allocs }, pool_size { o.pool_size }, pool { o.pool }, root { o.root }
            {
                o.reset();
            }

            constexpr locked_pool_resource& operator=(locked_pool_resource&& o) noexcept
            {
                using std::swap;
                swap(num_allocs, o.num_allocs);
                swap(pool_size, o.pool_size);
                swap(pool, o.pool);
                swap(root, o.root);
                return *this;
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
                if (pool != nullptr) memres.deallocate(pool, pool_size, alignof(pool_node));
                reset();
            }

            // Returns true if pool is unallocated
            bool empty() const noexcept
            {
                return num_allocs == 0;
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
            std::size_t max_size(std::size_t alignment = alignof(std::max_align_t)) const noexcept
            {
                if (root == nullptr) return 0;
                auto size = root->size;
                const auto overhead = alignment + sizeof(std::size_t) + sizeof(std::uint8_t);
                if (size < overhead) return 0;
                size -= overhead;
                if (size < sizeof(pool_node) + alignof(pool_node)) return 0;
                return size;
            }

            bool in_pool(void* ptr) const noexcept
            {
                auto p = reinterpret_cast<const std::byte*>(ptr);
                return p >= pool and p < static_cast<std::byte*>(pool) + pool_size;
            }

        protected:
            struct pool_node
            {
                std::size_t size;
                pool_node* next[2] { nullptr, nullptr };
                bool alloc_hi { false };

                auto* begin() noexcept { return reinterpret_cast<std::byte*>(this); }
                auto* end() noexcept { return reinterpret_cast<std::byte*>(this) + size; }
                auto* cbegin() const noexcept { return reinterpret_cast<const std::byte*>(this); }
                auto* cend() const noexcept { return reinterpret_cast<const std::byte*>(this) + size; }

                template<bool merge = true>
                constexpr pool_node* insert(pool_node* node) noexcept
                {
                    if (node == nullptr) return this;
                    const auto higher = node > this;
                    const auto lower = not higher;
                    auto*& n = next[higher];

                    auto fits_between = [](auto* a, auto* x, auto* b)
                    {
                        return ((x - a) xor (x - b)) < 0;
                    };

                    if constexpr (merge) if (n != nullptr)
                    {
                        auto lo = n, hi = node;
                        if (higher) std::swap(lo, hi);
                        if (lo->cend() == hi->cbegin())
                        {
                            lo->size += hi->size;
                            node = lo->insert(hi->next[1])->insert(hi->next[0]);
                            n = nullptr;
                        }
                    }

                    if (n != nullptr)
                    {
                        if (fits_between(this, node, n) and node->size >= n->size)
                        {
                            node = node->template insert<merge>(n->next[lower]);
                            n->next[lower] = nullptr;
                            node = node->template insert<merge>(n);
                        }
                        else node = n->template insert<merge>(node);
                    }

                    if constexpr (merge)
                    {
                        auto lo = node, hi = this;
                        if (higher) std::swap(lo, hi);
                        if (lo->cend() == hi->cbegin())
                        {
                            lo->size += hi->size;
                            if (higher) lo->next[1] = hi->next[1];
                            return lo->insert(hi->next[lower]);
                        }
                    }

                    if (node->size > size)
                    {
                        n = node->next[lower];
                        node->next[lower] = this;
                        return node;
                    }

                    n = node;
                    return this;
                }

                constexpr auto minmax() noexcept
                {
                    auto cmp = [](const auto& a, const auto& b) { return a == nullptr or (b != nullptr and a->size < b->size); };
                    return std::minmax({ next[0], next[1] }, cmp);
                }

                constexpr pool_node* erase() noexcept
                {
                    auto [min, max] = minmax();
                    return max->template insert<false>(min);
                }

                constexpr pool_node* replace(pool_node* node) noexcept
                {
                    auto next_size = [this](auto i) { return next[i] != nullptr ? next[i]->size : 0; };
                    auto max = std::max(next_size(0), next_size(1));
                    if (node->size > max) return node->template insert<false>(next[0])->template insert<false>(next[1]);
                    return erase()->template insert<false>(node);
                }

                constexpr pool_node* resize(std::size_t s) noexcept
                {
                    size = s;
                    auto [min, max] = minmax();
                    if (max != nullptr and max->size > size)
                    {
                        auto* node = max->template insert<false>(min);
                        next[0] = next[1] = nullptr;
                        return node->template insert<false>(this);
                    }
                    return this;
                }
            };

            constexpr void reset() noexcept
            {
                num_allocs = 0;
                pool_size = 0;
                pool = nullptr;
                root = nullptr;
            }

            static constexpr void* aligned_ptr(void* p, std::size_t align, bool down = false) noexcept
            {
                auto a = reinterpret_cast<std::uintptr_t>(p);
                auto b = a & -align;
                if (not down and b != a) b += align;
                return reinterpret_cast<void*>(b);
            }

            [[nodiscard]] virtual void* do_allocate(std::size_t n, std::size_t a) override
            {
                n += a + sizeof(std::size_t) + sizeof(std::uint8_t);
                n = std::max(n, sizeof(pool_node) + alignof(pool_node));

                auto ptr = [a](void* begin, std::size_t size)
                {
                    *static_cast<std::size_t*>(begin) = size;
                    auto* ptr = static_cast<std::uint8_t*>(begin);
                    auto* p = static_cast<std::uint8_t*>(aligned_ptr(ptr + sizeof(std::size_t) + sizeof(std::uint8_t), a));
                    *(p - 1) = p - ptr;
                    return p;
                };

                interrupt_mask no_interrupts_please { };
                if (root == nullptr) throw std::bad_alloc { };
                auto size = root->size;
                ++num_allocs;
                if (size > n + sizeof(pool_node) + alignof(pool_node))  // Split chunk
                {
                    auto split_at = root->alloc_hi ? root->end() - n : root->begin() + n;
                    auto* p = root->begin();
                    auto* q = static_cast<std::byte*>(aligned_ptr(split_at, alignof(pool_node), root->alloc_hi));
                    std::size_t p_size = q - p;
                    std::size_t q_size = size - p_size;
                    if (root->alloc_hi)
                    {
                        std::swap(p, q);
                        std::swap(p_size, q_size);
                        root->resize(q_size);
                    }
                    else root = root->replace(new(q) pool_node { static_cast<std::uintptr_t>(q_size) });
                    root->alloc_hi ^= true;
                    return ptr(p, p_size);
                }
                else if (size >= n)                                     // Use entire chunk
                {
                    auto* p = ptr(root->begin(), size);
                    root = root->erase();
                    return p;
                }
                --num_allocs;
                throw std::bad_alloc { };
            }

            virtual void do_deallocate(void* ptr, std::size_t, std::size_t) noexcept override
            {
                auto* p = static_cast<std::uint8_t*>(ptr);
                p -= *(p - 1);
                pool_node* node = new (p) pool_node { *reinterpret_cast<std::size_t*>(p) };
                interrupt_mask no_interrupts_please { };
                if (root == nullptr) root = node;
                else root = root->insert(node);
                --num_allocs;
            }

            virtual bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
            {
                return &other == static_cast<const std::pmr::memory_resource*>(this);
            }

            [[no_unique_address]] locking_memory_resource memres { };
            std::size_t num_allocs { 0 };
            std::size_t pool_size;
            void* pool;
            pool_node* root;
        };

        // Legacy allocator based on locked_pool_resource
        template<bool lock_self = true, typename T = std::byte>
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
                auto n = memres->max_size(alignof(T));
                return n / sizeof(T);
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
