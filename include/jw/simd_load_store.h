#/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
#/*    Copyright (C) 2023 - 2023 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <jw/simd.h>

namespace jw
{
    template<simd, std::indirectly_readable I> requires (std::is_arithmetic_v<std::iter_value_t<I>>)
    [[gnu::always_inline]] inline auto simd_load(format_nosimd, I src)
    {
        return *src;
    }

    template<simd, typename T, std::indirectly_writable<T> O> requires (std::is_arithmetic_v<std::iter_value_t<O>>)
    [[gnu::always_inline]] inline void simd_store(format_nosimd, O dst, T src)
    {
        *dst = src;
    }

    template<simd, std::contiguous_iterator I> requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) == 1)
    [[gnu::always_inline]] inline __m64 simd_load(format_pi8, I src)
    {
        return *reinterpret_cast<const __m64*>(&*src);
    }

    template<simd, std::contiguous_iterator I> requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) == 1)
    [[gnu::always_inline]] inline void simd_store(format_pi8, I dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(&*dst) = src;
    }

    template<simd flags, std::contiguous_iterator I> requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) <= 2)
    [[gnu::always_inline]] inline __m64 simd_load(format_pi16, const I src)
    {
        if constexpr (sizeof(std::iter_value_t<I>) == 2) return *reinterpret_cast<const __m64*>(&*src);
        else
        {
            __m64 data = simd_load<flags>(pi8, src);
            __m64 sign = _mm_setzero_si64();
            if constexpr (std::is_signed_v<std::iter_value_t<I>>) sign = _mm_cmpgt_pi8(sign, data);
            data = _mm_unpacklo_pi8(data, sign);
            return data;
        }
    }

    template<simd, std::contiguous_iterator I> requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) == 2)
    [[gnu::always_inline]] inline void simd_store(format_pi16, I dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(&*dst) = src;
    }

    template<simd, std::contiguous_iterator I> requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) == 1)
    [[gnu::always_inline]] inline void simd_store(format_pi16, I dst, __m64 src)
    {
        const __m64 a = std::is_signed_v<std::iter_value_t<I>> ? _mm_packs_pi16(src, src) : _mm_packs_pu16(src, src);
        *reinterpret_cast<std::uint32_t*>(&*dst) = _mm_cvtsi64_si32(a);
    }

    template<simd flags, std::contiguous_iterator I> requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) <= 4)
    [[gnu::always_inline]] inline __m64 simd_load(format_pi32, const I src)
    {
        if constexpr (sizeof(std::iter_value_t<I>) == 4) return *reinterpret_cast<const __m64*>(&*src);
        else
        {
            __m64 data = simd_load<flags>(pi16, src);
            __m64 sign = _mm_setzero_si64();
            if constexpr (std::is_signed_v<std::iter_value_t<I>>) sign = _mm_cmpgt_pi16(sign, data);
            data = _mm_unpacklo_pi16(data, sign);
            return data;
        }
    }

    template<simd, std::contiguous_iterator I> requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) == 4)
    [[gnu::always_inline]] inline void simd_store(format_pi32, I dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(&*dst) = src;
    }

    template<simd, std::contiguous_iterator I> requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) == 2)
    [[gnu::always_inline]] inline void simd_store(format_pi32, I dst, __m64 src)
    {
        const __m64 a = _mm_packs_pi32(src, src);
        *reinterpret_cast<std::uint32_t*>(&*dst) = _mm_cvtsi64_si32(a);
    }

    template<simd, std::contiguous_iterator I> requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) == 1)
    [[gnu::always_inline]] inline void simd_store(format_pi32, I dst, __m64 src)
    {
        __m64 data = _mm_packs_pi32(src, src);
        data = std::is_signed_v<std::iter_value_t<I>> ? _mm_packs_pi16(data, data) : _mm_packs_pu16(data, data);
        *reinterpret_cast<std::uint16_t*>(&*dst) = _mm_cvtsi64_si32(data);
    }

    template<simd flags, std::contiguous_iterator I> requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) <= 8)
    [[gnu::always_inline]] inline __m64 simd_load(format_si64, const I src)
    {
        if constexpr (sizeof(std::iter_value_t<I>) == 8) return *reinterpret_cast<const __m64*>(&*src);
        else
        {
            __m64 data = simd_load<flags>(pi32, src);
            __m64 sign = _mm_setzero_si64();
            if constexpr (std::is_signed_v<std::iter_value_t<I>>) sign = _mm_cmpgt_pi32(sign, data);
            data = _mm_unpacklo_pi32(data, sign);
            return data;
        }
    }

    template<simd, typename I> requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) == 8)
    [[gnu::always_inline]] inline void simd_store(format_si64, I dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(&*dst) = src;
    }

    template<simd, std::contiguous_iterator I> requires (std::same_as<std::iter_value_t<I>, float>)
    [[gnu::always_inline]] inline __m128 simd_load(format_ps, I src)
    {
        return *reinterpret_cast<const __m128*>(&*src);
    }

    template<simd, std::contiguous_iterator I> requires (std::signed_integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) == 4)
    [[gnu::always_inline]] inline __m128 simd_load(format_ps, I src)
    {
        const __m64 lo = *reinterpret_cast<const __m64*>(&*(src + 0));
        const __m64 hi = *reinterpret_cast<const __m64*>(&*(src + 2));
        return _mm_cvtpi32x2_ps(lo, hi);
    }

    template<simd flags, std::contiguous_iterator I> requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) <= 2)
    [[gnu::always_inline]] inline __m128 simd_load(format_ps, I src)
    {
        const __m64 data = simd_load<flags>(pi16, src);
        return std::is_signed_v<std::iter_value_t<I>> ? _mm_cvtpi16_ps(data) : _mm_cvtpu16_ps(data);
    }

    template<simd, std::contiguous_iterator I> requires (std::same_as<std::iter_value_t<I>, float>)
    [[gnu::always_inline]] inline void simd_store(format_ps, I dst, __m128 src)
    {
        *reinterpret_cast<__m128*>(&*dst) = src;
    }

    template<simd flags, std::contiguous_iterator I>
    requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) <= 4 and (std::is_signed_v<std::iter_value_t<I>> or sizeof(std::iter_value_t<I>) == 1))
    [[gnu::always_inline]] inline void simd_store(format_ps, I dst, __m128 src)
    {
        const __m64 lo = _mm_cvtps_pi32(src);
        const __m64 hi = _mm_cvtps_pi32(_mm_movehl_ps(src, src));
        if constexpr (simd_storable<I, flags, format_pi16>)
            simd_store<flags>(pi16, dst, _mm_packs_pi32(lo, hi));
        else
        {
            simd_store<flags>(pi32, dst + 0, lo);
            simd_store<flags>(pi32, dst + 2, hi);
        }
    }

    template<simd, std::contiguous_iterator I> requires (std::same_as<std::iter_value_t<I>, float>)
    [[gnu::always_inline]] inline __m64 simd_load(format_pf, I src)
    {
        return *reinterpret_cast<const __m64*>(&*src);
    }

    template<simd flags, std::contiguous_iterator I>
    requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) <= 4 and (std::is_signed_v<std::iter_value_t<I>> or sizeof(std::iter_value_t<I>) <= 2))
    [[gnu::always_inline]] inline __m64 simd_load(format_pf, const I src)
    {
        return _m_pi2fd(simd_load<flags>(pi32, src));
    }

    template<simd, std::contiguous_iterator I> requires (std::same_as<std::iter_value_t<I>, float>)
    [[gnu::always_inline]] inline void simd_store(format_pf, I dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(&*dst) = src;
    }

    template<simd flags, std::contiguous_iterator I>
    requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) <= 4 and (std::is_signed_v<std::iter_value_t<I>> or sizeof(std::iter_value_t<I>) == 1))
    [[gnu::always_inline]] inline void simd_store(format_pf, I dst, __m64 src)
    {
        simd_store<flags>(pi32, dst, _m_pf2id(src));
    }
}
