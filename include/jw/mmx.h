/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <mmintrin.h>
#include <xmmintrin.h>
#include <mm3dnow.h>
#include <jw/simd.h>

namespace jw
{
    template<simd flags>
    inline void mmx_empty() noexcept
    {
        if constexpr (flags & simd::amd3dnow) asm volatile ("femms");
        else if constexpr (flags & simd::mmx) asm volatile ("emms");
    }

    template<simd flags>
    struct mmx_guard
    {
        ~mmx_guard() { mmx_empty<flags>(); }
    };

    // This wrapper function helps avoid mixing MMX and x87 code.
    template<simd flags, typename F, typename... A>
    [[gnu::noinline]] inline decltype(auto) mmx_function(F&& func, A&&... args)
    {
        mmx_guard<flags> guard { };
        return std::forward<F>(func)(std::forward<A>(args)...);
    }
}