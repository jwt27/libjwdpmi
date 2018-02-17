/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstdint>
#include <jw/dpmi/lock.h>
#include <jw/dpmi/alloc.h>

namespace jw::dpmi::detail
{
    struct interrupt_id : public class_lock<interrupt_id>
    {
        locked_pool_allocator<> alloc { 4_KB };
        std::uint64_t id_count { 0 };
        std::uint32_t use_count { 0 };
        struct id_t
        {
            std::uint64_t id;
            std::uint32_t vector;
            bool acknowledged { false };
            constexpr id_t(std::uint64_t i, ::uint32_t v) : id(i), vector(v) { }
        };
        std::vector<std::shared_ptr<id_t>, locked_pool_allocator<>> current_interrupt { alloc };

        static void push_back(auto&&... args)
        { 
            auto* self = get();
            self->current_interrupt.push_back(std::allocate_shared<id_t>(self->alloc, self->id_count++, std::forward<decltype(args)>(args)...));
        }
        static void pop_back() { get()->current_interrupt.pop_back(); }

        static bool is_current_interrupt(const auto* p) noexcept { return p != nullptr and get()->current_interrupt.back().get()->id == p->id; }
        static auto get_current_interrupt() noexcept
        {
            using weak = std::weak_ptr<const id_t>;
            auto* self = get();
            if (self->current_interrupt.empty()) return weak { };
            return weak { self->current_interrupt.back() };
        }

        static void acknowledge() noexcept
        {
            auto* self = get();
            if (self->current_interrupt.empty()) return;
            self->current_interrupt.back()->acknowledged = true;
        }

        static interrupt_id* get()
        {
            if (__builtin_expect(instance == nullptr, false))
                instance = new interrupt_id { };
            return instance;
        }

        static void delete_if_possible()
        {
            if (__builtin_expect(get()->use_count > 0, true)) return;
            if (__builtin_expect(instance == nullptr, false)) return;

            delete instance;
            instance = nullptr;
        }
    private:
        inline static interrupt_id* instance { nullptr };
    };
}
