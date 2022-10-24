/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <utility>
#include <jw/simd_flags.h>
#include "jwdpmi_config.h"

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
#       define SIMD_SELECT_CALL(X) return (std::forward<F>(func).template operator()<X>(std::forward<A>(args)...))
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
