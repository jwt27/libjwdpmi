/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstdint>
#include <limits>
#include <numeric>
#include <span>
#include <jw/simd.h>

namespace jw::audio
{
    using sample_u8 = std::uint8_t;
    using sample_i16 = std::int16_t;
    using sample_i32 = std::int32_t;
    using sample_f32 = float;

    template<typename T>
    concept sample_type = any_of<T, sample_u8, sample_i16, sample_i32, sample_f32>;

    template<typename T, typename... U>
    concept any_sample_type_of = sample_type<T> and (sample_type<U> and ...) and any_of<T, U...>;

    template<typename D>
    concept sample_data = sample_type<simd_type<D>>;

    template<sample_type T>
    struct sample_traits
    {
        static consteval auto min()           { return std::numeric_limits<T>::min(); }
        static consteval auto max()           { return std::numeric_limits<T>::max(); }
        static consteval auto zero()          { return std::midpoint(max(), min()); }
        static consteval auto max_amplitude() { return std::max(std::abs(min() - zero()), std::abs(max() - zero())); }
    };

    template<>
    struct sample_traits<sample_f32>
    {
        static consteval sample_f32 min() noexcept    { return -1.f; }
        static consteval sample_f32 max() noexcept    { return +1.f; }
        static consteval sample_f32 zero() noexcept   { return  0.f; }
        static consteval sample_f32 max_amplitude()   { return  1.f; }
    };

    template<sample_type To>
    struct sample_convert_t
    {
        template<typename From>
        struct conversion_data
        {
            static constexpr auto src0 = sample_traits<From>::zero();
            static constexpr auto dst0 = sample_traits<To>::zero();
            static constexpr float factor = sample_traits<To>::max_amplitude() / sample_traits<From>::max_amplitude();
            static constexpr int rshift = sizeof(From) * 8 - sizeof(To) * 8;
        };

        template<simd flags, sample_data D>
        auto operator()(format_nosimd, D src)
        {
            using From = simd_type<D>;
            using cvt = conversion_data<From>;
            if constexpr (std::floating_point<To> or std::floating_point<From>)
            {
                float a = src;
                a -= cvt::src0;
                a *= cvt::factor;
                a += cvt::dst0;
                return simd_return(nosimd, simd_data<To>(a));
            }
            else
            {
                int a = src;
                a -= cvt::src0;
                if constexpr (cvt::rshift > 0) a >>= cvt::rshift;
                else a <<= -cvt::rshift;
                a += cvt::dst0;
                return simd_return(nosimd, simd_data<To>(a));
            }
        }

        template<simd flags, sample_data D>
        auto operator()(format_pi8, D src)
        {
            using cvt = conversion_data<simd_type<D>>;
            __m64 dst = src;
            static_assert (cvt::rshift == 0);
            if constexpr (cvt::src0 != 0) dst = _mm_sub_pi8(dst, _mm_set1_pi8(cvt::src0));
            if constexpr (cvt::dst0 != 0) dst = _mm_add_pi8(dst, _mm_set1_pi8(cvt::dst0));
            return simd_return(pi8, simd_data<To>(dst));
        }

        template<simd flags, sample_data D>
        auto operator()(format_pi16, D src)
        {
            using cvt = conversion_data<simd_type<D>>;
            __m64 dst = src;
            if constexpr (cvt::src0 != 0) dst = _mm_sub_pi16(dst, _mm_set1_pi16(cvt::src0));
            if constexpr (cvt::rshift != 0)
            {
                if constexpr (cvt::rshift > 0) dst = _mm_srai_pi16(dst, cvt::rshift);
                else dst = _mm_slli_pi16(dst, -cvt::rshift);
            }
            if constexpr (cvt::dst0 != 0) dst = _mm_add_pi16(dst, _mm_set1_pi16(cvt::dst0));
            return simd_return(pi16, simd_data<To>(dst));
        }

        template<simd flags, sample_data D>
        auto operator()(format_pi32, D src)
        {
            using cvt = conversion_data<simd_type<D>>;
            __m64 dst = src;
            if constexpr (cvt::src0 != 0) dst = _mm_sub_pi32(dst, _mm_set1_pi32(cvt::src0));
            if constexpr (cvt::rshift != 0)
            {
                if constexpr (cvt::rshift > 0) dst = _mm_srai_pi32(dst, cvt::rshift);
                else dst = _mm_slli_pi32(dst, -cvt::rshift);
            }
            if constexpr (cvt::dst0 != 0) dst = _mm_add_pi32(dst, _mm_set1_pi32(cvt::dst0));
            return simd_return(pi32, simd_data<To>(dst));
        }

        template<simd flags, sample_data D>
        auto operator()(format_pf, D src)
        {
            using cvt = conversion_data<simd_type<D>>;
            constexpr auto set1 = [](float f) consteval { return reinterpret_cast<__m64>(simd_vector<float, 2> { f, f }); };
            __m64 dst = src;
            if constexpr (cvt::src0 != 0) dst = _m_pfsub(dst, set1(cvt::src0));
            if constexpr (cvt::factor != 1) dst = _m_pfmul(dst, set1(cvt::factor));
            if constexpr (cvt::dst0 != 0) dst = _m_pfadd(dst, set1(cvt::dst0));
            return simd_return(pf, simd_data<To>(dst));
        }

        template<simd flags, sample_data D>
        auto operator()(format_ps, D src)
        {
            using cvt = conversion_data<simd_type<D>>;
            __m128 dst = src;
            if constexpr (cvt::src0 != 0) dst = _mm_sub_ps(dst, _mm_set1_ps(cvt::src0));
            if constexpr (cvt::factor != 1) dst = _mm_mul_ps(dst, _mm_set1_ps(cvt::factor));
            if constexpr (cvt::dst0 != 0) dst = _mm_add_ps(dst, _mm_set1_ps(cvt::dst0));
            return simd_return(ps, simd_data<To>(dst));
        }
    };

    template<sample_type T>
    inline constexpr sample_convert_t<T> sample_convert;
}
