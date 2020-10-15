/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <memory>
#include <memory_resource>
#include <algorithm>
#include <span>
#include <array>

namespace jw
{
    template<typename Alloc>
    struct allocator_delete : public Alloc
    {
        constexpr allocator_delete() = default;
        constexpr allocator_delete(auto&& other) : Alloc(std::forward<decltype(other)>(other)) { }
        using traits = std::allocator_traits<Alloc>;
        void operator()(auto* p)
        {
            if (p == nullptr) return;
            traits::destroy(*this, p);
            traits::deallocate(*this, p, 1);
        }
    };

    // Allocate a std::unique_ptr with a custom allocator
    template<typename T, typename Alloc, typename... Args>
    auto allocate_unique(const Alloc& alloc, Args&&... args)
    {
        using rebind = typename std::allocator_traits<Alloc>::template rebind_alloc<T>;
        using traits = std::allocator_traits<rebind>;
        using deleter = allocator_delete<rebind>;

        deleter d { rebind { alloc } };
        auto* p = traits::allocate(d, 1);
        try { traits::construct(d, p, std::forward<Args>(args)...); }
        catch (...) { traits::deallocate(d, p, 1); throw; }

        return std::unique_ptr<T, deleter> { p, std::move(d) };
    }

    // Initialize a non-owning std::unique_ptr with allocator_delete (saves typing)
    template<typename T, typename Alloc>
    auto init_unique(const Alloc& alloc)
    {
        using rebind = typename std::allocator_traits<Alloc>::template rebind_alloc<T>;
        using deleter = allocator_delete<rebind>;

        return std::unique_ptr<T, deleter> { nullptr, deleter { rebind { alloc } } };
    }

    struct basic_pool_resource : public std::pmr::memory_resource
    {
        constexpr basic_pool_resource() noexcept = default;
        virtual ~basic_pool_resource() noexcept = default;

        basic_pool_resource(const std::span<std::byte>& ptr) { grow(ptr); }

        constexpr basic_pool_resource(basic_pool_resource&& o) noexcept
            : num_allocs { std::move(o.num_allocs) }, root { std::move(o.root) } { o.reset(); }

        constexpr basic_pool_resource& operator=(basic_pool_resource&& o) noexcept
        {
            using std::swap;
            swap(num_allocs, o.num_allocs);
            swap(root, o.root);
            return *this;
        }

        basic_pool_resource(const basic_pool_resource&) = delete;
        basic_pool_resource& operator=(const basic_pool_resource&) = delete;

        constexpr bool empty() const noexcept { return num_allocs == 0; }

        void grow(const std::span<std::byte>& ptr) noexcept
        {
            auto* n = new(ptr.data()) pool_node { ptr.size_bytes() };
            if (root == nullptr) root = n;
            else root = root->insert(n);
        }

        // Returns the size of the largest chunk.
        constexpr std::size_t max_chunk_size() const noexcept
        {
            if (root == nullptr) return 0;
            return root->size;
        }

        // Returns maximum number of bytes that can be allocated at once, with the given alignment.
        constexpr std::size_t max_size(std::size_t alignment = alignof(std::max_align_t)) const noexcept
        {
            auto size = max_chunk_size();
            const auto overhead = alignment + sizeof(std::size_t) + sizeof(std::uint8_t);
            if (size < overhead) return 0;
            size -= overhead;
            if (size < sizeof(pool_node) + alignof(pool_node)) return 0;
            return size;
        }

    protected:
        struct pool_node
        {
            std::size_t size;
            std::array<pool_node*, 2> next { nullptr, nullptr };
            bool alloc_hi { false };

            constexpr auto* begin() noexcept { return static_cast<std::byte*>(static_cast<void*>(this)); }
            constexpr auto* end() noexcept { return begin() + size; }

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
                    if (lo->end() == hi->begin())
                    {
                        lo->size += hi->size;
                        node = lo->insert(hi->next[1])->insert(hi->next[0]);
                        std::destroy_at(hi);
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
                    if (lo->end() == hi->begin())
                    {
                        lo->size += hi->size;
                        if (higher) lo->next[1] = hi->next[1];
                        auto* node = lo->insert(hi->next[lower]);
                        std::destroy_at(hi);
                        return node;
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
                return std::minmax_element(next.begin(), next.end(), cmp);
            }

            constexpr pool_node* erase() noexcept
            {
                auto [min, max] = minmax();
                auto* node = (*max)->template insert<false>(*min);
                std::destroy_at(this);
                return node;
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
                if (*max != nullptr and (*max)->size > size)
                {
                    auto* node = (*max)->template insert<false>(*min);
                    next[0] = next[1] = nullptr;
                    return node->template insert<false>(this);
                }
                return this;
            }
        };

        constexpr void reset() noexcept
        {
            num_allocs = 0;
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
            grow({ reinterpret_cast<std::byte*>(p), *reinterpret_cast<std::size_t*>(p) });
            --num_allocs;
        }

        virtual bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
        {
            return dynamic_cast<const basic_pool_resource*>(&other) == this;
        }

        std::size_t num_allocs { 0 };
        pool_node* root { nullptr };
    };

    struct pool_resource : public basic_pool_resource
    {
        using base = basic_pool_resource;

        constexpr pool_resource(std::pmr::memory_resource* upstream = std::pmr::get_default_resource()) noexcept
            : res { upstream } { };

        pool_resource(std::size_t size_bytes, std::pmr::memory_resource* upstream = std::pmr::get_default_resource())
            : pool_resource { upstream } { grow(size_bytes); }

        virtual ~pool_resource() noexcept { release(); }

        constexpr pool_resource(pool_resource&& o) noexcept
            : base { std::move(o) }, num_pools { std::move(o.num_pools) }, pools { std::move(o.pools) } { o.reset(); }

        constexpr pool_resource& operator=(pool_resource&& o) noexcept
        {
            base::operator=(std::move(o));
            using std::swap;
            swap(res, o.res);
            swap(num_pools, o.num_pools);
            swap(pools, o.pools);
            return *this;
        }

        pool_resource(const pool_resource&) = delete;
        pool_resource& operator=(const pool_resource&) = delete;

        constexpr std::size_t size() const noexcept
        {
            std::size_t size = 0;
            for (unsigned i = 0; i < num_pools; ++i)
                size += pools[i].size_bytes();
            return size;
        }

        // Deallocate the memory pool
        void release() noexcept
        {
            if (pools != nullptr)
            {
                for (unsigned i = 0; i < num_pools; ++i)
                    res->deallocate(pools[i].data(), pools[i].size_bytes(), alignof(pool_node));
                res->deallocate(pools, sizeof(pool_type) * num_pools, alignof(pool_type));
            }
            reset();
        }

        void grow(std::size_t bytes)
        {
            bytes = std::max(bytes, sizeof(pool_node));
            auto* p = res->allocate(bytes, alignof(pool_node));
            pool_type* new_pools;
            try
            {
                new_pools = static_cast<pool_type*>(res->allocate(sizeof(pool_type) * (num_pools + 1), alignof(pool_type)));
            }
            catch (...)
            {
                res->deallocate(p, bytes, alignof(pool_node));
                throw;
            }
            if (pools != nullptr)
            {
                std::uninitialized_move(pools, pools + num_pools, new_pools);
                std::destroy_n(pools, num_pools);
                res->deallocate(pools, sizeof(pool_type) * num_pools, alignof(pool_type));
            }
            pools = new_pools;
            grow(pools[num_pools++] = pool_type { static_cast<std::byte*>(p), bytes });
        }

        bool in_pool(const void* ptr) const noexcept
        {
            if (pools == nullptr) return false;
            auto p = static_cast<const std::byte*>(ptr);
            for (unsigned i = 0; i < num_pools; ++i)
                if (p < (&*pools[i].end()) and p >= &*pools[i].begin()) return true;
            return false;
        }

    protected:
        using base::grow;

        constexpr void reset() noexcept
        {
            base::reset();
            num_pools = 0;
            pools = nullptr;
        }

        using pool_type = std::span<std::byte>;

        std::pmr::memory_resource* res { };
        std::size_t num_pools { 0 };
        pool_type* pools { nullptr };
    };
}
