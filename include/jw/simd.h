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

    template<typename T> concept can_load_pi8  = requires (const T* p) { { load_pi8 (p) } -> std::same_as<__m64 >; };
    template<typename T> concept can_load_pi16 = requires (const T* p) { { load_pi16(p) } -> std::same_as<__m64 >; };
    template<typename T> concept can_load_pi32 = requires (const T* p) { { load_pi32(p) } -> std::same_as<__m64 >; };
    template<typename T> concept can_load_si64 = requires (const T* p) { { load_pi64(p) } -> std::same_as<__m64 >; };
    template<typename T> concept can_load_pf   = requires (const T* p) { { load_pif (p) } -> std::same_as<__m64 >; };
    template<typename T> concept can_load_ps   = requires (const T* p) { { load_pis (p) } -> std::same_as<__m128>; };

    template<typename T> concept can_store_pi8  = requires (T* p, __m64 v) { { store_pi8 (p, v) }; };
    template<typename T> concept can_store_pi16 = requires (T* p, __m64 v) { { store_pi16(p, v) }; };
    template<typename T> concept can_store_pi32 = requires (T* p, __m64 v) { { store_pi32(p, v) }; };
    template<typename T> concept can_store_si64 = requires (T* p, __m64 v) { { store_pi64(p, v) }; };
    template<typename T> concept can_store_pf   = requires (T* p, __m64 v) { { store_pif (p, v) }; };
    template<typename T> concept can_store_ps   = requires (T* p, __m64 v) { { store_pis (p, v) }; };

    template<std::integral T> requires (sizeof(T) == 1)
    [[gnu::always_inline]] inline __m64 load_pi8(const T* src)
    {
        return *reinterpret_cast<const __m64*>(src);
    }

    template<std::integral T> requires (sizeof(T) == 1)
    [[gnu::always_inline]] inline void store_pi8(T* dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(dst) = src;
    }

    template<std::integral T> requires (sizeof(T) == 2 or can_load_pi8<T>)
    [[gnu::always_inline]] inline __m64 load_pi16(const T* src)
    {
        if constexpr (sizeof(T) == 2) return *reinterpret_cast<const __m64*>(src);
        __m64 data = load_pi8(src);
        __m64 sign = _mm_setzero_si64();
        if constexpr (std::is_signed_v<T>) sign = _mm_cmpgt_pi8(sign, data);
        data = _mm_unpacklo_pi8(data, sign);
        return data;
    }

    template<std::integral T> requires (sizeof(T) == 2)
    [[gnu::always_inline]] inline void store_pi16(T* dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(dst) = src;
    }

    template<std::integral T> requires (sizeof(T) == 1)
    [[gnu::always_inline]] inline void store_pi16(T* dst, __m64 src)
    {
        const __m64 a = std::is_signed_v<T> ? _mm_packs_pi16(src, src) : _mm_packs_pu16(src, src);
        *reinterpret_cast<std::uint32_t*>(dst) = _mm_cvtsi64_si32(a);
    }

    template<std::integral T> requires (sizeof(T) == 4 or can_load_pi16<T>)
    [[gnu::always_inline]] inline __m64 load_pi32(const T* src)
    {
        if constexpr (sizeof(T) == 4) return *reinterpret_cast<const __m64*>(src);
        __m64 data = load_pi16(src);
        __m64 sign = _mm_setzero_si64();
        if constexpr (std::is_signed_v<T>) sign = _mm_cmpgt_pi16(sign, data);
        data = _mm_unpacklo_pi16(data, sign);
        return data;
    }

    template<std::integral T> requires (sizeof(T) == 4)
    [[gnu::always_inline]] inline void store_pi32(T* dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(dst) = src;
    }

    template<std::signed_integral T> requires (sizeof(T) == 2)
    [[gnu::always_inline]] inline void store_pi32(T* dst, __m64 src)
    {
        const __m64 a = _mm_packs_pi32(src, src);
        *reinterpret_cast<std::uint32_t*>(dst) = _mm_cvtsi64_si32(a);
    }

    template<std::integral T> requires (sizeof(T) == 1)
    [[gnu::always_inline]] inline void store_pi32(T* dst, __m64 src)
    {
        __m64 data = _mm_packs_pi32(src, src);
        data = std::is_signed_v<T> ? _mm_packs_pi16(data, data) : _mm_packs_pu16(data, data);
        *reinterpret_cast<std::uint16_t*>(dst) = _mm_cvtsi64_si32(data);
    }

    template<std::integral T> requires (sizeof(T) == 8 or can_load_pi32<T>)
    [[gnu::always_inline]] inline __m64 load_si64(const T* src)
    {
        if constexpr (sizeof(T) == 8) return *reinterpret_cast<const __m64*>(src);
        __m64 data = load_pi32(src);
        __m64 sign = _mm_setzero_si64();
        if constexpr (std::is_signed_v<T>) sign = _mm_cmpgt_pi32(sign, data);
        data = _mm_unpacklo_pi32(data, sign);
        return data;
    }

    template<std::integral T> requires (sizeof(T) == 8)
    [[gnu::always_inline]] inline void store_si64(T* dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(dst) = src;
    }

    [[gnu::always_inline]] inline __m128 load_ps(const float* src)
    {
        return *reinterpret_cast<const __m128*>(src);
    }

    template<std::signed_integral T> requires (sizeof(T) == 4)
    [[gnu::always_inline]] inline __m128 load_ps(const T* src)
    {
        const __m64 lo = *reinterpret_cast<__m64*>(src + 0);
        const __m64 hi = *reinterpret_cast<__m64*>(src + 2);
        return _mm_cvtpi32x2_ps(lo, hi);
    }

    template<std::integral T> requires (can_load_pi16<T>)
    [[gnu::always_inline]] inline __m128 load_ps(const T* src)
    {
        const __m64 data = load_pi16(src);
        return std::is_signed_v<T> ? _mm_cvtpi16_ps(data) : _mm_cvtpu16_ps(data);
    }

    [[gnu::always_inline]] inline void store_ps(float* dst, __m128 src)
    {
        *reinterpret_cast<__m128*>(dst) = src;
    }

    template<std::integral T> requires ((can_store_pi16<T> or can_store_pi32<T>) and (std::is_signed_v<T> or sizeof(T) == 1))
    [[gnu::always_inline]] inline void store_ps(T* dst, __m128 src)
    {
        const __m64 lo = _mm_cvtps_pi32(src);
        const __m64 hi = _mm_cvtps_pi32(_mm_movehl_ps(src, src));
        if constexpr (can_store_pi16<T>)
            store_pi16(dst, _mm_packs_pi32(lo, hi));
        else
        {
            store_pi32(dst + 0, lo);
            store_pi32(dst + 2, hi);
        }
    }

    [[gnu::always_inline]] inline __m64 load_pf(const float* src)
    {
        return *reinterpret_cast<const __m64*>(src);
    }

    template<std::integral T> requires (can_load_pi32<T> and (std::is_signed_v<T> or sizeof(T) <= 2))
    [[gnu::always_inline]] inline __m64 load_pf(const T* src)
    {
        return _m_pi2fd(load_pi32(src));
    }

    [[gnu::always_inline]] inline void store_pf(float* dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(dst) = src;
    }

    template<std::integral T> requires (can_store_pi32<T> and (std::is_signed_v<T> or sizeof(T) == 1))
    [[gnu::always_inline]] inline void store_pf(T* dst, __m64 src)
    {
        store_pi32(dst, _m_pf2id(src));
    }
}
