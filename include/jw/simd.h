/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <type_traits>
#include <concepts>
#include <xmmintrin.h>
#include <mm3dnow.h>
#include <jw/simd_flags.h>

namespace jw
{
    template<typename T, std::size_t N>
    using simd_vector [[gnu::vector_size(sizeof(T) * N)]] = T;

    consteval std::uint8_t shuffle_mask(unsigned v0, unsigned v1, unsigned v2, unsigned v3)
    {
        return ((v3 & 3) << 6) | ((v2 & 3) << 4) | ((v1 & 3) << 2) | (v0 & 3);
    }

    // Using these types in template parameters (std::same_as, etc) prevents
    // ignored-attribute warnings.
    using m64_t = simd_vector<int, 2>;
    using m128_t = simd_vector<float, 4>;

    struct tag_pi8  { } inline constexpr pi8;
    struct tag_pi16 { } inline constexpr pi16;
    struct tag_pi32 { } inline constexpr pi32;
    struct tag_si64 { } inline constexpr si64;
    struct tag_ps   { } inline constexpr ps;
    struct tag_pf   { } inline constexpr pf;

    template<typename Tag>
    using simd_type_for_tag = std::conditional_t<std::same_as<Tag, tag_ps>, m128_t, m64_t>;

    template<typename T, typename Tag> concept can_load = requires (Tag t, const T* p) { { simd_load(t, p) } -> std::same_as<simd_type_for_tag<Tag>>; };
    template<typename T, typename Tag> concept can_store = requires (Tag t, T* p, simd_type_for_tag<Tag> v) { simd_store(t, p, v); };

    template<std::integral T> requires (sizeof(T) == 1)
    [[gnu::always_inline]] inline __m64 simd_load(tag_pi8, const T* src)
    {
        return *reinterpret_cast<const __m64*>(src);
    }

    template<std::integral T> requires (sizeof(T) == 1)
    [[gnu::always_inline]] inline void simd_store(tag_pi8, T* dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(dst) = src;
    }

    template<std::integral T> requires (sizeof(T) <= 2)
    [[gnu::always_inline]] inline __m64 simd_load(tag_pi16, const T* src)
    {
        if constexpr (sizeof(T) == 2) return *reinterpret_cast<const __m64*>(src);
        else
        {
            __m64 data = simd_load(pi8, src);
            __m64 sign = _mm_setzero_si64();
            if constexpr (std::is_signed_v<T>) sign = _mm_cmpgt_pi8(sign, data);
            data = _mm_unpacklo_pi8(data, sign);
            return data;
        }
    }

    template<std::integral T> requires (sizeof(T) == 2)
    [[gnu::always_inline]] inline void simd_store(tag_pi16, T* dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(dst) = src;
    }

    template<std::integral T> requires (sizeof(T) == 1)
    [[gnu::always_inline]] inline void simd_store(tag_pi16, T* dst, __m64 src)
    {
        const __m64 a = std::is_signed_v<T> ? _mm_packs_pi16(src, src) : _mm_packs_pu16(src, src);
        *reinterpret_cast<std::uint32_t*>(dst) = _mm_cvtsi64_si32(a);
    }

    template<std::integral T> requires (sizeof(T) <= 4)
    [[gnu::always_inline]] inline __m64 simd_load(tag_pi32, const T* src)
    {
        if constexpr (sizeof(T) == 4) return *reinterpret_cast<const __m64*>(src);
        else
        {
            __m64 data = simd_load(pi16, src);
            __m64 sign = _mm_setzero_si64();
            if constexpr (std::is_signed_v<T>) sign = _mm_cmpgt_pi16(sign, data);
            data = _mm_unpacklo_pi16(data, sign);
            return data;
        }
    }

    template<std::integral T> requires (sizeof(T) == 4)
    [[gnu::always_inline]] inline void simd_store(tag_pi32, T* dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(dst) = src;
    }

    template<std::signed_integral T> requires (sizeof(T) == 2)
    [[gnu::always_inline]] inline void simd_store(tag_pi32, T* dst, __m64 src)
    {
        const __m64 a = _mm_packs_pi32(src, src);
        *reinterpret_cast<std::uint32_t*>(dst) = _mm_cvtsi64_si32(a);
    }

    template<std::integral T> requires (sizeof(T) == 1)
    [[gnu::always_inline]] inline void simd_store(tag_pi32, T* dst, __m64 src)
    {
        __m64 data = _mm_packs_pi32(src, src);
        data = std::is_signed_v<T> ? _mm_packs_pi16(data, data) : _mm_packs_pu16(data, data);
        *reinterpret_cast<std::uint16_t*>(dst) = _mm_cvtsi64_si32(data);
    }

    template<std::integral T> requires (sizeof(T) <= 8)
    [[gnu::always_inline]] inline __m64 simd_load(tag_si64, const T* src)
    {
        if constexpr (sizeof(T) == 8) return *reinterpret_cast<const __m64*>(src);
        else
        {
            __m64 data = simd_load(pi32, src);
            __m64 sign = _mm_setzero_si64();
            if constexpr (std::is_signed_v<T>) sign = _mm_cmpgt_pi32(sign, data);
            data = _mm_unpacklo_pi32(data, sign);
            return data;
        }
    }

    template<std::integral T> requires (sizeof(T) == 8)
    [[gnu::always_inline]] inline void simd_store(tag_si64, T* dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(dst) = src;
    }

    [[gnu::always_inline]] inline __m128 simd_load(tag_ps, const float* src)
    {
        return *reinterpret_cast<const __m128*>(src);
    }

    template<std::signed_integral T> requires (sizeof(T) == 4)
    [[gnu::always_inline]] inline __m128 simd_load(tag_ps, const T* src)
    {
        const __m64 lo = *reinterpret_cast<__m64*>(src + 0);
        const __m64 hi = *reinterpret_cast<__m64*>(src + 2);
        return _mm_cvtpi32x2_ps(lo, hi);
    }

    template<std::integral T> requires (sizeof(T) <= 2)
    [[gnu::always_inline]] inline __m128 simd_load(tag_ps, const T* src)
    {
        const __m64 data = simd_load(pi16, src);
        return std::is_signed_v<T> ? _mm_cvtpi16_ps(data) : _mm_cvtpu16_ps(data);
    }

    [[gnu::always_inline]] inline void simd_store(tag_ps, float* dst, __m128 src)
    {
        *reinterpret_cast<__m128*>(dst) = src;
    }

    template<std::integral T> requires (sizeof(T) <= 4 and (std::is_signed_v<T> or sizeof(T) == 1))
    [[gnu::always_inline]] inline void simd_store(tag_ps, T* dst, __m128 src)
    {
        const __m64 lo = _mm_cvtps_pi32(src);
        const __m64 hi = _mm_cvtps_pi32(_mm_movehl_ps(src, src));
        if constexpr (can_store<T, tag_pi16>)
            simd_store(pi16, dst, _mm_packs_pi32(lo, hi));
        else
        {
            simd_store(pi32, dst + 0, lo);
            simd_store(pi32, dst + 2, hi);
        }
    }

    [[gnu::always_inline]] inline __m64 simd_load(tag_pf, const float* src)
    {
        return *reinterpret_cast<const __m64*>(src);
    }

    template<std::integral T> requires (sizeof(T) <= 4 and (std::is_signed_v<T> or sizeof(T) <= 2))
    [[gnu::always_inline]] inline __m64 simd_load(tag_pf, const T* src)
    {
        return _m_pi2fd(simd_load(pi32, src));
    }

    [[gnu::always_inline]] inline void simd_store(tag_pf, float* dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(dst) = src;
    }

    template<std::integral T> requires (sizeof(T) <= 4 and (std::is_signed_v<T> or sizeof(T) == 1))
    [[gnu::always_inline]] inline void simd_store(tag_pf, T* dst, __m64 src)
    {
        simd_store(pi32, dst, _m_pf2id(src));
    }
}
