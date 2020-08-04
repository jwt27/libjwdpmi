/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstdint>
#include <jw/dpmi/lock.h>
#include <jw/dpmi/alloc.h>
#include <cxxabi.h>

namespace jw::dpmi::detail
{
    struct jw_cxa_eh_globals
    {
        void *caughtExceptions { nullptr };
        unsigned int uncaughtExceptions { 0 };
    } inline eh_globals;

    struct interrupt_id : public class_lock<interrupt_id>
    {
        locked_pool_allocator<false> alloc { 1_KB };
        std::uint64_t id_count { 0 };
        std::uint32_t use_count { 0 };
        struct id_t
        {
            const std::uint64_t id;
            const std::uint32_t vector;
            enum { interrupt, exception } const type;
            bool acknowledged { type == exception };
            constexpr id_t(std::uint64_t i, std::uint32_t v, auto t) noexcept : id(i), vector(v), type(t) { }
            jw_cxa_eh_globals eh_globals { };
        };
        std::vector<std::shared_ptr<id_t>, locked_pool_allocator<false, std::shared_ptr<id_t>>> current_interrupt { alloc };

        static void push_back(auto&&... args)
        {
            auto* self = get();
            if (self->current_interrupt.size() == 0) eh_globals = *reinterpret_cast<jw_cxa_eh_globals*>(abi::__cxa_get_globals());
            self->current_interrupt.push_back(std::allocate_shared<id_t>(self->alloc, self->id_count++, std::forward<decltype(args)>(args)...));
            *reinterpret_cast<jw_cxa_eh_globals*>(abi::__cxa_get_globals()) = self->current_interrupt.back()->eh_globals;
        }
        static void pop_back()
        {
            auto* self = get();
            self->current_interrupt.pop_back();
            *reinterpret_cast<jw_cxa_eh_globals*>(abi::__cxa_get_globals()) = (self->current_interrupt.size() == 0) ? eh_globals : self->current_interrupt.back()->eh_globals;
        }

        static bool is_current_interrupt(const auto* p) noexcept { return p != nullptr and get()->current_interrupt.back()->id == p->id; }
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
