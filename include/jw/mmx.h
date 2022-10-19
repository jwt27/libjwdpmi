/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <mmintrin.h>
#include <mm3dnow.h>
#include <jw/simd.h>

// These functions emulate the "mmx2" intrinsics in <xmmintrin.h>.  Those have
// a target("sse") attribute, and can not be used with target("3dnowa").
namespace jw::inline mmx2
{
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wpsabi"

    template<unsigned N>
    [[using gnu: gnu_inline, always_inline, artificial]]
    inline int mmx2_extract_pi16(__m64 a)
    {
        return static_cast<std::int16_t>(__builtin_ia32_vec_ext_v4hi(reinterpret_cast<__v4hi>(a), N));
    }

    template<unsigned N>
    [[using gnu: gnu_inline, always_inline, artificial]]
    inline __m64 mmx2_insert_pi16(__m64 a, std::int16_t v)
    {
        return reinterpret_cast<__m64>(__builtin_ia32_vec_set_v4hi(reinterpret_cast<__v4hi>(a), v, N));
    }

    [[using gnu: gnu_inline, always_inline, artificial]]
    inline __m64 mmx2_max_pi16(__m64 a, __m64 b)
    {
        return reinterpret_cast<__m64>(__builtin_ia32_pmaxsw(reinterpret_cast<__v4hi>(a), reinterpret_cast<__v4hi>(b)));
    }

    [[using gnu: gnu_inline, always_inline, artificial]]
    inline __m64 mmx2_max_pu8(__m64 a, __m64 b)
    {
        return reinterpret_cast<__m64>(__builtin_ia32_pmaxub(reinterpret_cast<__v8qi>(a), reinterpret_cast<__v8qi>(b)));
    }

    [[using gnu: gnu_inline, always_inline, artificial]]
    inline __m64 mmx2_min_pi16(__m64 a, __m64 b)
    {
        return reinterpret_cast<__m64>(__builtin_ia32_pminsw(reinterpret_cast<__v4hi>(a), reinterpret_cast<__v4hi>(b)));
    }

    [[using gnu: gnu_inline, always_inline, artificial]]
    inline __m64 mmx2_min_pu8(__m64 a, __m64 b)
    {
        return reinterpret_cast<__m64>(__builtin_ia32_pminub(reinterpret_cast<__v8qi>(a), reinterpret_cast<__v8qi>(b)));
    }

    [[using gnu: gnu_inline, always_inline, artificial]]
    inline int mmx2_movemask_pi8(__m64 a)
    {
        return __builtin_ia32_pmovmskb(reinterpret_cast<__v8qi>(a));
    }

    [[using gnu: gnu_inline, always_inline, artificial]]
    inline __m64 mmx2_mulhi_pu16(__m64 a, __m64 b)
    {
        return reinterpret_cast<__m64>(__builtin_ia32_pmulhuw(reinterpret_cast<__v4hi>(a), reinterpret_cast<__v4hi>(b)));
    }

    template<std::uint8_t mask>
    [[using gnu: gnu_inline, always_inline, artificial]]
    inline __m64 mmx2_shuffle_pi16(__m64 a)
    {
        return reinterpret_cast<__m64>(__builtin_ia32_pshufw(reinterpret_cast<__v4hi>(a), mask));
    }

    [[using gnu: gnu_inline, always_inline, artificial]]
    inline void mmx2_maskmove_si64(__m64 a, __m64 n, void* p)
    {
        __builtin_ia32_maskmovq(reinterpret_cast<__v8qi>(a), reinterpret_cast<__v8qi>(n), static_cast<char*>(p));
    }

    [[using gnu: gnu_inline, always_inline, artificial]]
    inline __m64 mmx2_avg_pu8(__m64 a, __m64 b)
    {
        return reinterpret_cast<__m64>(__builtin_ia32_pavgb(reinterpret_cast<__v8qi>(a), reinterpret_cast<__v8qi>(b)));
    }

    [[using gnu: gnu_inline, always_inline, artificial]]
    inline __m64 mmx2_avg_pu16(__m64 a, __m64 b)
    {
        return reinterpret_cast<__m64>(__builtin_ia32_pavgw(reinterpret_cast<__v4hi>(a), reinterpret_cast<__v4hi>(b)));
    }

    [[using gnu: gnu_inline, always_inline, artificial]]
    inline __m64 mmx2_sad_pu8(__m64 a, __m64 b)
    {
        return reinterpret_cast<__m64>(__builtin_ia32_psadbw(reinterpret_cast<__v8qi>(a), reinterpret_cast<__v8qi>(b)));
    }

    [[using gnu: gnu_inline, always_inline, artificial]]
    inline void mmx2_stream_pi(__m64* p, __m64 a)
    {
        __builtin_ia32_movntq(reinterpret_cast<std::uint64_t*>(p), reinterpret_cast<std::uint64_t>(a));
    }

#   pragma GCC diagnostic pop
}

namespace jw
{
    template<simd flags>
    [[gnu::always_inline]] inline void mmx_empty() noexcept
    {
        if constexpr (flags & simd::amd3dnow) asm volatile ("femms");
        else if constexpr (flags & simd::mmx) asm volatile ("emms");
    }

    template<simd flags>
    struct mmx_guard
    {
        [[gnu::always_inline]] ~mmx_guard() { mmx_empty<flags>(); }
    };

    // This wrapper function helps avoid mixing MMX and x87 code.
    template<simd flags, typename F, typename... A>
    [[gnu::noinline]] inline decltype(auto) mmx_function(F&& func, A&&... args)
    {
        mmx_guard<flags> guard { };
        return std::forward<F>(func)(std::forward<A>(args)...);
    }

    template<simd flags, unsigned N>
    [[gnu::always_inline]] inline std::int16_t mmx_extract_pi16(__m64 src)
    {
        if constexpr (flags.match(simd::mmx2)) return mmx2_extract_pi16<N>(src);
        return reinterpret_cast<simd_vector<std::int16_t, 4>>(src)[N];
    }

    template<simd flags, unsigned N>
    [[gnu::always_inline]] inline __m64 mmx_insert_pi16(__m64 dst, std::int16_t v)
    {
        if constexpr (flags.match(simd::mmx2)) return mmx2_insert_pi16<N>(dst, v);
        auto dst2 = reinterpret_cast<simd_vector<std::int16_t, 4>>(dst);
        dst2[N] = v;
        return reinterpret_cast<__m64>(dst2);
    }

    template<simd flags, unsigned N, std::int16_t V>
    [[gnu::always_inline]] inline __m64 mmx_insert_constant_pi16(__m64 dst)
    {
        if constexpr (flags.match(simd::mmx2)) return mmx2_insert_pi16<N>(dst, V);
        constexpr auto a = [](unsigned n) -> std::uint16_t { return N == n ? 0 : 0xffff; };
        constexpr auto o = [](unsigned n) -> std::int16_t  { return N == n ? V : 0     ; };
        constexpr simd_vector<std::uint16_t, 4> and_mask { a(0), a(1), a(2), a(3) };
        constexpr simd_vector<std::int16_t , 4>  or_mask { o(0), o(1), o(2), o(3) };
        dst = _mm_and_si64(dst, reinterpret_cast<__m64>(and_mask));
        dst = _mm_or_si64 (dst, reinterpret_cast<__m64>( or_mask));
        return dst;
    }
}
