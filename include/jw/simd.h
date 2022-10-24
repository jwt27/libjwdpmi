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

    struct tag_nosimd { } inline constexpr nosimd;
    struct tag_pi8    { } inline constexpr pi8;
    struct tag_pi16   { } inline constexpr pi16;
    struct tag_pi32   { } inline constexpr pi32;
    struct tag_si64   { } inline constexpr si64;
    struct tag_ps     { } inline constexpr ps;
    struct tag_pf     { } inline constexpr pf;

    template<typename Tag> constexpr simd simd_flags_for_tag = simd::none;
    template<> constexpr simd simd_flags_for_tag<tag_pi8>  = simd::mmx;
    template<> constexpr simd simd_flags_for_tag<tag_pi16> = simd::mmx;
    template<> constexpr simd simd_flags_for_tag<tag_pi32> = simd::mmx;
    template<> constexpr simd simd_flags_for_tag<tag_si64> = simd::mmx;
    template<> constexpr simd simd_flags_for_tag<tag_ps>   = simd::sse;
    template<> constexpr simd simd_flags_for_tag<tag_pf>   = simd::amd3dnow;

    template<typename Tag> constexpr std::size_t simd_elements_for_tag = 0;
    template<> constexpr std::size_t simd_elements_for_tag<tag_nosimd> = 1;
    template<> constexpr std::size_t simd_elements_for_tag<tag_pi8>    = 8;
    template<> constexpr std::size_t simd_elements_for_tag<tag_pi16>   = 4;
    template<> constexpr std::size_t simd_elements_for_tag<tag_pi32>   = 2;
    template<> constexpr std::size_t simd_elements_for_tag<tag_si64>   = 1;
    template<> constexpr std::size_t simd_elements_for_tag<tag_ps>     = 4;
    template<> constexpr std::size_t simd_elements_for_tag<tag_pf>     = 2;

    template<typename Tag> struct simd_type_for_tag_helper { using type = void;   };
    template<> struct simd_type_for_tag_helper<tag_pi8>    { using type = m64_t;  };
    template<> struct simd_type_for_tag_helper<tag_pi16>   { using type = m64_t;  };
    template<> struct simd_type_for_tag_helper<tag_pi32>   { using type = m64_t;  };
    template<> struct simd_type_for_tag_helper<tag_si64>   { using type = m64_t;  };
    template<> struct simd_type_for_tag_helper<tag_ps>     { using type = m128_t; };
    template<> struct simd_type_for_tag_helper<tag_pf>     { using type = m64_t;  };

    template<typename Tag> using simd_type_for_tag = simd_type_for_tag_helper<Tag>::type;

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
        const __m64 lo = *reinterpret_cast<const __m64*>(src + 0);
        const __m64 hi = *reinterpret_cast<const __m64*>(src + 2);
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

    template<typename F, typename... A>
    concept simd_invocable = requires(F f, A&&... args) { f.template operator()<simd { }>(std::forward<A>(args)...); };

    template<simd flags, typename F, typename... A> requires (simd_invocable<F, A...>)
    [[gnu::always_inline, gnu::flatten]] decltype(auto) simd_invoke(F&& func, A&&... args)
    {
        return (std::forward<F>(func).template operator()<flags>(std::forward<A>(args)...));
    }

    // Apply given transform function to src and store result in dst.  Size
    // must be divisible by 8, and pointers must be 16-byte aligned.
    template<simd flags, typename To, typename From, typename F, typename... A>
    [[gnu::flatten, gnu::hot]] To* simd_transform(To* dst, const From* src, std::size_t n, F func, A... args)
    {
        std::size_t i = 0;

        constexpr auto can_invoke = []<typename Tag>(Tag) consteval
        {
            return flags.match(simd_flags_for_tag<Tag>) and can_load<From, Tag> and can_store<To, Tag> and simd_invocable<F, Tag, simd_type_for_tag<Tag>, A...>;
        };

        auto do_invoke = [&]<typename Tag>(Tag t)
        {
            simd_store(t, dst + i, simd_invoke<flags>(func, t, simd_load(t, src + i), args...));
            i += simd_elements_for_tag<Tag>;
        };

        while (i < n)
        {
            if constexpr (can_invoke(pi8)) do_invoke(pi8);
            else if constexpr (can_invoke(pi16)) do_invoke(pi16);
            else if constexpr (can_invoke(pi32)) do_invoke(pi32);
            else if constexpr (can_invoke(si64)) do_invoke(si64);
            else if constexpr (can_invoke(ps)) do_invoke(ps);
            else if constexpr (can_invoke(pf)) do_invoke(pf);
            else
            {
                dst[i] = func(nosimd, src[i], args...);
                i += 1;
            }
        }
        return dst + i;
    }
}
