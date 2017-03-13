#pragma once
#include <memory>

namespace jw
{
    template<typename Alloc>
    struct allocator_delete
    {
        Alloc alloc;
        allocator_delete(Alloc&& a) : alloc(a) { }

        void operator()(auto* p)
        {
            using T = std::remove_reference_t<decltype(*p)>;
            p->~T();
            alloc.deallocate(p, 1);
        }
    };

    template<typename T, typename Alloc, typename... Args>
    auto allocate_unique(const Alloc& alloc, Args&&... args)
    {
        using rebind = typename std::allocator_traits<Alloc>::template rebind_alloc<T>;
        using deleter = allocator_delete<rebind>;

        auto d = deleter { rebind { alloc } };
        auto* p = d.alloc.allocate(1);
        try { p = new(p) T { std::forward<Args>(args)... }; }
        catch (...) { d.alloc.deallocate(p, 1); throw; }

        return std::unique_ptr<T, deleter>(p, d);
    }
}
