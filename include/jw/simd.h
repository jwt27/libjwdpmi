/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <type_traits>
#include <jw/dpmi/cpuid.h>
#include <jw/simd_flags.h>
#include "jwdpmi_config.h"

namespace jw
{
    template<typename T, std::size_t N>
    using simd_vector [[gnu::vector_size(sizeof(T) * N)]] = T;

    consteval std::uint8_t shuffle_mask(unsigned v0, unsigned v1, unsigned v2, unsigned v3)
    {
        return ((v3 & 3) << 6) | ((v2 & 3) << 4) | ((v1 & 3) << 2) | (v0 & 3);
    }

    // Return the SIMD flags available for the default CPU target.
    constexpr inline simd default_simd() noexcept
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
    inline decltype(auto) simd_select(F&& func, A&&... args)
    {
        const auto flags = runtime_simd() | default_simd();
#       define SIMD_SELECT_CALL(X) return std::forward<F>(func).template operator()<X>(std::forward<A>(args)...)
#       define SIMD_SELECT_MATCH(X) if (flags.match(X)) SIMD_SELECT_CALL(X)
#       define SIMD_SELECT_TARGET(X) SIMD_SELECT_MATCH((X & config::allowed_simd) | default_simd())

        SIMD_SELECT_TARGET(simd_target::athlon_xp);
        SIMD_SELECT_TARGET(simd_target::athlon);
        SIMD_SELECT_TARGET(simd_target::pentium_3);
        SIMD_SELECT_TARGET(simd_target::k6_2);
        SIMD_SELECT_TARGET(simd_target::pentium_mmx);
        SIMD_SELECT_CALL(default_simd());

#       undef SIMD_SELECT_TARGET
#       undef SIMD_SELECT_MATCH
#       undef SIMD_SELECT_CALL
    }
}
