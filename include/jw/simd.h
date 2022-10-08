/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <type_traits>
#include <mmintrin.h>
#include <mm3dnow.h>
#include <jw/dpmi/cpuid.h>

namespace jw
{
    struct simd
    {
        enum flags
        {
            none        = 0b0,
            mmx         = 0b1,     // MMX
            mmx2        = 0b10,    // MMX extensions (introduced with SSE)
            amd3dnow    = 0b100,   // 3DNow!
            amd3dnow2   = 0b1000,  // 3DNow! extensions
            sse         = 0b10000  // SSE
        } value;

        constexpr simd() noexcept = default;
        constexpr simd(simd&&) noexcept = default;
        constexpr simd(const simd&) noexcept = default;
        constexpr simd& operator=(simd&&) noexcept = default;
        constexpr simd& operator=(const simd&) noexcept = default;

        constexpr simd(flags f) noexcept : value { f } { }
        constexpr simd& operator=(flags f) noexcept { value = f; return *this; }

        constexpr std::strong_ordering operator<=>(const simd&) const noexcept = default;
        constexpr explicit operator bool() const noexcept { return value != none; }

        constexpr simd& operator|=(simd a) noexcept { return *this = *this | a; }
        constexpr simd operator|(simd a) const noexcept { return { static_cast<flags>(value | a.value) }; }

        constexpr simd& operator&=(simd a) noexcept { return *this = *this & a; }
        constexpr simd operator&(simd a) const noexcept { return { static_cast<flags>(value & a.value) }; }

        constexpr bool match(simd target) const noexcept { return (*this & target) == target; }
    };

    constexpr simd operator|(simd::flags a, simd::flags b) noexcept { return simd { a } | simd { b }; }
    constexpr simd operator&(simd::flags a, simd::flags b) noexcept { return simd { a } & simd { b }; }

    // Return the SIMD flags available for the default CPU target.
    constexpr simd default_simd() noexcept
    {
        simd flags = simd::none;
#       ifdef __MMX__
        flags |= simd::mmx;
#       endif
#       ifdef __SSE__
        flags |= simd::mmx2 | simd::sse;
#       endif
#       ifdef __3dNOW__
        flags |= simd::amd3dnow;
#       endif
#       ifdef __3dNOW_A__
        flags |= simd::mmx2 | simd::amd3dnow2;
#       endif
        return flags;
    }

    // Return the SIMD flags supported by the runtime CPU.
    [[gnu::const]] inline simd runtime_simd() noexcept
    {
        constexpr simd unknown = static_cast<simd::flags>(~simd::none);
        static constinit simd flags = unknown;
        if (flags == unknown) [[unlikely]]
        {
            flags = simd::none;
            const auto cpu = dpmi::cpuid::feature_flags();
            if (cpu.mmx) flags |= simd::mmx;
            if (cpu.sse) flags |= simd::mmx2 | simd::sse;
            const auto amd = dpmi::cpuid::amd_feature_flags();
            if (amd.amd3dnow) flags |= simd::amd3dnow;
            if (amd.amd3dnow_extensions) flags |= simd::amd3dnow2;
            if (amd.mmx_extensions) flags |= simd::mmx2;
        }
        return flags;
    }

    template<simd flags = default_simd()>
    inline void mmx_empty() noexcept
    {
        if constexpr (flags & simd::amd3dnow) _m_femms();
        else if constexpr (flags & simd::mmx) _m_empty();
    }

    template<simd flags = default_simd()>
    struct mmx_guard
    {
        ~mmx_guard() { mmx_empty<flags>(); }
    };
}

namespace jw::simd_target
{
    constexpr simd none        = simd::none;
    constexpr simd pentium_mmx = simd::mmx;
    constexpr simd pentium_3   = simd::mmx | simd::mmx2 | simd::sse;
    constexpr simd k6_2        = simd::mmx | simd::amd3dnow;
    constexpr simd athlon      = simd::mmx | simd::amd3dnow | simd::mmx2 | simd::amd3dnow2;
    constexpr simd athlon_xp   = simd::mmx | simd::amd3dnow | simd::mmx2 | simd::amd3dnow2 | simd::sse;
}

namespace jw
{
    template<typename F, typename... A>
    decltype(auto) simd_select(F&& func, A&&... args)
    {
        const auto flags = runtime_simd();
#       define SIMD_SELECT_CALL(X) return std::forward<F>(func).template operator()<X>(std::forward<A>(args)...)
#       define SIMD_SELECT_MATCH(X) if (flags.match(X)) SIMD_SELECT_CALL(X)
        SIMD_SELECT_MATCH(simd_target::athlon_xp);
        SIMD_SELECT_MATCH(simd_target::athlon);
        SIMD_SELECT_MATCH(simd_target::pentium_3);
        SIMD_SELECT_MATCH(simd_target::k6_2);
        SIMD_SELECT_MATCH(simd_target::pentium_mmx);
        SIMD_SELECT_CALL(default_simd());
#       undef SIMD_SELECT_MATCH
#       undef SIMD_SELECT_CALL
    }
}
