/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <bit>
#include <array>
#include <mmintrin.h>
#include <mm3dnow.h>
#include <jw/simd.h>
#include <jw/math.h>

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
        if constexpr (flags & simd::amd3dnow) _m_femms();
        else if constexpr (flags & simd::mmx) _mm_empty();
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

    template<simd flags, std::uint8_t mask>
    [[gnu::always_inline]] inline __m64 mmx_shuffle_pi16(__m64 src)
    {
        if constexpr (flags.match(simd::mmx2)) return mmx2_shuffle_pi16<mask>(src);
        constexpr std::array<unsigned, 4> i { (mask >> 0) & 3, (mask >> 2) & 3, (mask >> 4) & 3, (mask >> 6) & 3 };
        const auto v = reinterpret_cast<simd_vector<std::int16_t, 4>>(src);
        return _mm_setr_pi16(v[i[0]], v[i[1]], v[i[2]], v[i[3]]);
    }

    // Round an unsigned fixed-point MMX vector.
    template<simd flags, unsigned frac_bits>
    [[gnu::always_inline]] inline __m64 mmx_round_pu16(__m64 src)
    {
        static_assert (frac_bits < 16);
        constexpr std::int16_t x = 1 << (frac_bits - 1);
        constexpr simd_vector<std::int16_t, 4> add { x, x, x, x };
        return _mm_srli_pi16(_mm_adds_pu16(src, reinterpret_cast<__m64>(add)), frac_bits);
    }

    // Round a signed fixed-point MMX vector.
    template<simd flags, unsigned frac_bits>
    [[gnu::always_inline]] inline __m64 mmx_round_pi16(__m64 src)
    {
        static_assert (frac_bits < 16);
        constexpr std::int16_t x = 1 << (frac_bits - 1);
        constexpr simd_vector<std::int16_t, 4> add { x, x, x, x };
        return _mm_srai_pi16(_mm_adds_pi16(src, reinterpret_cast<__m64>(add)), frac_bits);
    }

    // Multiply an unsigned MMX vector by a floating-point constant, with rounding.
    template<simd flags, bool rounding, std::array<long double, 4> mul, std::uint16_t input_max>
    [[gnu::always_inline]] inline __m64 mmx_fmul_pu16(__m64 src) noexcept
    {
        constexpr bool input_overflow = input_max > 0x7fff;

        constexpr auto product = [](long double x) constexpr
        {
            return std::array<long double, 4> { x * mul[0], x * mul[1], x * mul[2], x * mul[3] };
        };

        constexpr std::uint16_t output_max = [product]() constexpr
        {
            auto x = product(input_max);
            return std::max({ round(x[0]), round(x[1]), round(x[2]), round(x[3]) });
        }();

        constexpr auto factor = [product](unsigned frac_bits) consteval
        {
            auto x = product(1 << frac_bits);
            simd_vector<std::uint16_t, 4> v
            {
                static_cast<std::uint16_t>(round(x[0])),
                static_cast<std::uint16_t>(round(x[1])),
                static_cast<std::uint16_t>(round(x[2])),
                static_cast<std::uint16_t>(round(x[3]))
            };
            return reinterpret_cast<__m64>(v);
        };

        constexpr auto frac_bits = [](bool unsigned_mul) consteval
        {
            auto mul_max = std::max({ mul[0], mul[1], mul[2], mul[3] });
            unsigned max_bits = unsigned_mul ? 16 : 15;
            unsigned max_frac = ((1 << max_bits) - 1) / mul_max;
            unsigned bits = std::bit_width(max_frac) - 1;
            unsigned dst_bits = std::bit_width(output_max);
            return std::min((bits < 16 ? 16 : 32) - dst_bits, bits);
        };

        constexpr auto all_one = []() consteval
        {
            for (auto f : mul) if (f != 1) return false;
            return true;
        };

        constexpr auto all_int = []() consteval
        {
            for (auto f : mul) if (static_cast<int>(f) != f) return false;
            return true;
        };

        if constexpr (all_one()) return src;
        if constexpr (all_int()) return _mm_mullo_pi16(src, factor(0));

        if constexpr (flags.match(simd::amd3dnow) and frac_bits(false) >= 16 and rounding and not input_overflow)
        {
            src = _m_pmulhrw(src, factor(16));
        }
        else if constexpr (flags.match(simd::mmx2) and frac_bits(true) >= 16 + rounding)
        {
            constexpr unsigned bits = rounding ? frac_bits(true) : 16;
            src = mmx2_mulhi_pu16(src, factor(bits));
            if constexpr (rounding) src = mmx_round_pu16<flags, bits - 16>(src);
        }
        else if constexpr (frac_bits(false) >= 16 + rounding + input_overflow)
        {
            constexpr unsigned bits = rounding ? frac_bits(false) : 16 + input_overflow;
            if constexpr (input_overflow) src = _mm_srli_pi16(src, 1);
            src = _mm_mulhi_pi16(src, factor(bits));
            if constexpr (rounding) src = mmx_round_pu16<flags, bits - input_overflow - 16>(src);
        }
        else
        {
            constexpr unsigned bits = frac_bits(true);
            if constexpr (input_overflow and bits >= 1) src = _mm_srli_pi16(src, 1);
            constexpr bool do_round = rounding and bits > input_overflow + 1;
            src = _mm_mullo_pi16(src, factor(bits));
            if constexpr (do_round) src = mmx_round_pu16<flags, bits - input_overflow>(src);
            else if constexpr (bits > input_overflow) src = _mm_srli_pi16(src, bits - input_overflow);
        }

        return src;
    }

    // Multiply and divide an unsigned MMX vector by unsigned constants, with rounding.
    template<simd flags, bool round, std::array<int, 4> mul, std::array<int, 4> div, std::uint16_t input_max>
    [[gnu::always_inline]] inline __m64 mmx_muldiv_pu16(__m64 src) noexcept
    {
        constexpr std::array<long double, 4> factor
        {
            static_cast<long double>(mul[0]) / div[0],
            static_cast<long double>(mul[1]) / div[1],
            static_cast<long double>(mul[2]) / div[2],
            static_cast<long double>(mul[3]) / div[3]
        };
        return mmx_fmul_pu16<flags, round, factor, input_max>(src);
    }

    // Multiply and divide an unsigned MMX vector by unsigned scalar constants, with rounding.
    template<simd flags, bool round, int mul, int div, std::uint16_t input_max>
    [[gnu::always_inline]] inline __m64 mmx_muldiv_scalar_pu16(__m64 src) noexcept
    {
        constexpr std::array<int, 4> vmul { mul, mul, mul, mul };
        constexpr std::array<int, 4> vdiv { div, div, div, div };
        return mmx_muldiv_pu16<flags, round, vmul, vdiv, input_max>(src);
    }

    // Divide an unsigned MMX vector by an unsigned constant, with rounding.
    template<simd flags, bool round, std::array<int, 4> div, std::uint16_t input_max>
    [[gnu::always_inline]] inline __m64 mmx_div_pu16(__m64 src) noexcept
    {
        constexpr std::array<int, 4> vmul { 1, 1, 1, 1 };
        return mmx_muldiv_pu16<flags, round, vmul, div, input_max>(src);
    }

    // Divide an unsigned MMX vector by an unsigned scalar constant, with rounding.
    template<simd flags, bool round, int div, std::uint16_t input_max>
    [[gnu::always_inline]] inline __m64 mmx_div_scalar_pu16(__m64 src) noexcept
    {
        if constexpr (div == 1) return src;
        constexpr std::array<int, 4> vmul { 1, 1, 1, 1 };
        constexpr std::array<int, 4> vdiv { div, div, div, div };
        return mmx_muldiv_pu16<flags, round, vmul, vdiv, input_max>(src);
    }
}
