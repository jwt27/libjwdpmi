/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <optional>
#include <jw/dpmi/memory.h>

namespace jw::dpmi
{
    namespace detail
    {
        inline selector ring0_cs { 0 };
        inline selector ring3_cs { get_cs() };
        inline selector ring0_ss { 0 };
        inline selector ring3_ss { get_ss() };
    }

    struct no_ring0_access : std::runtime_error
    {
        no_ring0_access() : runtime_error("Switch to ring 0 failed.") { }
    };

    struct ring0_privilege
    {
        [[gnu::noinline]] ring0_privilege();
        [[gnu::noinline]] ~ring0_privilege();

        static void force_leave()
        {
            if (get_cs() == detail::ring0_cs) leave();
        }

    private:
        inline static std::optional<descriptor> cs;
        inline static std::optional<descriptor> ss;
        inline static std::optional<descriptor> gate;
        inline static far_ptr32 entry;
        inline static std::uintptr_t esp;
        bool dont_leave { false };
        
        [[gnu::naked, gnu::noinline]] static void enter();
        [[gnu::naked, gnu::noinline]] static void leave();

        [[gnu::naked]] static void ring0_entry_point();
    };
}
