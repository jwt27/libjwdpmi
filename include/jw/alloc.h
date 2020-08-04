/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <memory>

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
            p = nullptr;
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
        try { p = new(p) T { std::forward<Args>(args)... }; }
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

    //template <typename T> using pmr_unique_ptr = std::unique_ptr<T, allocator_delete<std::pmr::polymorphic_allocator<T>>>;
}
