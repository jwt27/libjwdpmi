#/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
#/*    Copyright (C) 2022 - 2023 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <cstdint>
#include <limits>
#include <numeric>
#include <span>
#include <jw/simd.h>
#include <jw/mmx.h>

namespace jw::audio
{
    using sample_u8 = std::uint8_t;
    using sample_i16 = std::int16_t;
    using sample_i32 = std::int32_t;
    using sample_f32 = float;

    template<typename T>
    concept sample_type = any_of<std::remove_const_t<T>, sample_u8, sample_i16, sample_i32, sample_f32>;

    template<typename T, typename... U>
    concept any_sample_type_of = sample_type<T> and (sample_type<U> and ...) and any_of<std::remove_const_t<T>, U...>;

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

    template<sample_type... T>
    struct sample_convert_t
    {
        template<typename To>
        struct impl
        {
            template<typename From>
            struct conversion_data
            {
                static constexpr auto src0 = sample_traits<From>::zero();
                static constexpr auto dst0 = sample_traits<To>::zero();
                static constexpr float factor = sample_traits<To>::max_amplitude() / sample_traits<From>::max_amplitude();
                static constexpr int rshift = sizeof(From) * 8 - sizeof(To) * 8;
            };

            template<simd flags, simd_data_for<To> D>
            auto operator()(simd_format auto, D src) const
            {
                return src;
            }

            template<simd flags, sample_data D>
            auto operator()(format_nosimd, D src) const
            {
                using From = simd_type<D>;
                using cvt = conversion_data<From>;
                if constexpr (std::floating_point<To> or std::floating_point<From>)
                {
                    float a = src;
                    a -= cvt::src0;
                    a *= cvt::factor;
                    a += cvt::dst0;
                    return simd_data<To>(a);
                }
                else
                {
                    int a = src;
                    a -= cvt::src0;
                    if constexpr (cvt::rshift > 0) a >>= cvt::rshift;
                    else a <<= -cvt::rshift;
                    a += cvt::dst0;
                    return simd_data<To>(a);
                }
            }

            template<simd flags, sample_data D>
            requires (std::max(sizeof(simd_type<D>), sizeof(To)) <= 1)
            auto operator()(format_pi8, D src) const
            {
                using cvt = conversion_data<simd_type<D>>;
                __m64 dst = src;
                static_assert (cvt::rshift == 0);
                if constexpr (cvt::src0 != 0) dst = _mm_sub_pi8(dst, _mm_set1_pi8(cvt::src0));
                if constexpr (cvt::dst0 != 0) dst = _mm_add_pi8(dst, _mm_set1_pi8(cvt::dst0));
                return simd_data<To>(dst);
            }

            template<simd flags, sample_data D>
            requires (std::max(sizeof(simd_type<D>), sizeof(To)) <= 2)
            auto operator()(format_pi16, D src) const
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
                return simd_data<To>(dst);
            }

            template<simd flags, sample_data D>
            requires (std::max(sizeof(simd_type<D>), sizeof(To)) <= 4)
            auto operator()(format_pi32, D src) const
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
                return simd_data<To>(dst);
            }

            template<simd flags, sample_data D>
            auto operator()(format_pf, D src) const
            {
                using cvt = conversion_data<simd_type<D>>;
                constexpr auto set1 = [](float f) consteval { return reinterpret_cast<__m64>(simd_vector<float, 2> { f, f }); };
                __m64 dst = src;
                if constexpr (cvt::src0 != 0) dst = _m_pfsub(dst, set1(cvt::src0));
                if constexpr (cvt::factor != 1) dst = _m_pfmul(dst, set1(cvt::factor));
                if constexpr (cvt::dst0 != 0) dst = _m_pfadd(dst, set1(cvt::dst0));
                return simd_data<To>(dst);
            }

            template<simd flags, sample_data D>
            auto operator()(format_ps, D src) const
            {
                using cvt = conversion_data<simd_type<D>>;
                __m128 dst = src;
                if constexpr (cvt::src0 != 0) dst = _mm_sub_ps(dst, _mm_set1_ps(cvt::src0));
                if constexpr (cvt::factor != 1) dst = _mm_mul_ps(dst, _mm_set1_ps(cvt::factor));
                if constexpr (cvt::dst0 != 0) dst = _mm_add_ps(dst, _mm_set1_ps(cvt::dst0));
                return simd_data<To>(dst);
            }
        };

        template<simd flags, simd_format F, sample_data... D>
        requires (sizeof...(D) == sizeof...(T) and (simd_invocable<impl<T>, flags, F, D> and ...))
        auto operator()(F, D... src) const
        {
            return simd_return(F { }, simd_invoke<flags>(impl<T> { }, F { }, src)...);
        }
    };

    template<sample_type... To>
    inline constexpr sample_convert_t<To...> sample_convert;

    // Interleave samples from left and right channels:
    // { L0 L1 L2 L3 }, { R0 R1 R2 R3 } -> { L0 R0 L1 R1 }, { L2 R2 L3 R3 }
    struct sample_interleave_t
    {
        template<simd flags, simd_format F, sample_data D>
        auto operator()(F, D l, D r) const
        {
            using T = simd_type<D>;
            if constexpr (std::same_as<F, format_pi8>)
            {
                auto lo = _mm_unpacklo_pi8(l, r);
                auto hi = _mm_unpackhi_pi8(l, r);
                return simd_return(F { }, simd_data<T>(lo), simd_data<T>(hi));
            }
            else if constexpr (std::same_as<F, format_pi16>)
            {
                auto lo = _mm_unpacklo_pi16(l, r);
                auto hi = _mm_unpackhi_pi16(l, r);
                return simd_return(F { }, simd_data<T>(lo), simd_data<T>(hi));
            }
            else if constexpr (any_of<F, format_pi32, format_pf>)
            {
                auto lo = _mm_unpacklo_pi32(l, r);
                auto hi = _mm_unpackhi_pi32(l, r);
                return simd_return(F { }, simd_data<T>(lo), simd_data<T>(hi));
            }
            else if constexpr (std::same_as<F, format_ps>)
            {
                auto lo = _mm_unpacklo_ps(l, r);
                auto hi = _mm_unpackhi_ps(l, r);
                return simd_return(F { }, simd_data<T>(lo), simd_data<T>(hi));
            }
            else
            {
                return simd_return(F { }, simd_data<T>(l), simd_data<T>(r));
            }
        }
    } inline constexpr sample_interleave;

    // De-interleave samples to separate left and right channels:
    // { L0 R0 L1 R1 }, { L2 R2 L3 R3 } -> { L0 L1 L2 L3 }, { R0 R1 R2 R3 }
    struct sample_separate_t
    {
        template<simd flags, any_simd_format_of<format_nosimd, format_si64> F, sample_data D>
        auto operator()(F, D lo, D hi) const
        {
            using T = simd_type<D>;
            return simd_return(F { }, simd_data<T>(lo), simd_data<T>(hi));
        }

        template<simd flags, sample_data D>
        auto operator()(format_ps, D lo, D hi) const
        {
            using T = simd_type<D>;
            auto lr0 = _mm_unpacklo_ps(lo, hi);
            auto lr1 = _mm_unpackhi_ps(lo, hi);
            auto l = _mm_movelh_ps(lr0, lr1);
            auto r = _mm_movehl_ps(lr1, lr0);
            return simd_return(ps, simd_data<T>(l), simd_data<T>(r));
        }

        template<simd flags, any_simd_format_of<format_pi8, format_pi16, format_pi32, format_pf> F, sample_data D>
        auto operator()(F, D lo, D hi) const
        {
            using T = simd_type<D>;
            __m64 x, l = lo, r = hi;
            if constexpr (any_of<F, format_pi8>)
            {
                x = _mm_unpacklo_pi8(l, r);
                r = _mm_unpackhi_pi8(l, r);
                l = x;
            }
            if constexpr (any_of<F, format_pi8, format_pi16>)
            {
                x = _mm_unpacklo_pi16(l, r);
                r = _mm_unpackhi_pi16(l, r);
                l = x;
            }
            x = _mm_unpacklo_pi32(l, r);
            r = _mm_unpackhi_pi32(l, r);
            l = x;
            return simd_return(F { }, simd_data<T>(l), simd_data<T>(r));
        }
    } inline constexpr sample_separate;

    // Mix multiple sample streams into one.
    class sample_mix_t
    {
        template<sample_data... Ds> requires (all_same<Ds...>)
        using type = simd_type<std::tuple_element_t<0, std::tuple<Ds...>>>;

        template<typename... Ds>
        static constexpr auto factor = 1.f / sizeof...(Ds);

        template<typename... Ds>
        static constexpr auto shift = std::countr_zero(std::bit_ceil(sizeof...(Ds)));

        template<simd_format Fmt, typename... Ds>
        static constexpr auto headroom = (simd_format_traits<Fmt>::element_size - sizeof(type<Ds...>)) * 8;

        template<typename... Ds>
        static auto mix_ps(__m128 first, Ds... next)
        {
            if constexpr (sizeof...(Ds) > 0) return _mm_add_ps(first, mix_ps(next...));
            else return first;
        }

        template<typename... Ds>
        static auto mix_pf(__m64 first, Ds... next)
        {
            if constexpr (sizeof...(Ds) > 0) return _m_pfadd(first, mix_pf(next...));
            else return first;
        }

        template<typename... Ds>
        static auto mix_pi32_oversized(__m64 first, Ds... next)
        {
            if constexpr (sizeof...(Ds) > 0) return _mm_add_pi32(first, mix_pi32_oversized(next...));
            else return first;
        }

        template<typename... Ds>
        static auto mix_pi16_oversized(__m64 first, Ds... next)
        {
            if constexpr (sizeof...(Ds) > 0) return _mm_add_pi16(first, mix_pi16_oversized(next...));
            else return first;
        }

        template<typename T, unsigned shift, typename... Ds>
        static auto mix_pi32(__m64 first, Ds... next)
        {
            __m64 x;
            if constexpr (std::unsigned_integral<T>)
                x = _mm_srli_pi32(first, shift);
            else
                x = _mm_srai_pi32(first, shift);

            if constexpr (sizeof...(Ds) > 0)
                return _mm_add_pi32(x, mix_pi32<T, shift>(next...));
            else
                return x;
        }

        template<typename T, unsigned shift, typename... Ds>
        static auto mix_pi16(__m64 first, Ds... next)
        {
            __m64 x;
            if constexpr (std::unsigned_integral<T>)
                x = _mm_srli_pi16(first, shift);
            else
                x = _mm_srai_pi16(first, shift);

            if constexpr (sizeof...(Ds) > 0)
                return _mm_add_pi16(x, mix_pi16<T, shift>(next...));
            else
                return x;
        }

        template<typename T, std::size_t... I, typename... Ds>
        static auto mix_pu16(std::tuple<Ds...> tuple, std::index_sequence<I...>)
        {
            if constexpr (sizeof...(Ds) > 2)
                return simd_return(pi16, simd_data<T>(mmx2_avg_pu16(std::get<I * 2>(tuple), std::get<I * 2 + 1>(tuple)))...);
            else
                return simd_data<T>(mmx2_avg_pu16(std::get<0>(tuple), std::get<1>(tuple)));
        };

        template<typename T, std::size_t... I, typename... Ds>
        static auto mix_pu8(std::tuple<Ds...> tuple, std::index_sequence<I...>)
        {
            if constexpr (sizeof...(Ds) > 2)
                return simd_return(pi8, simd_data<T>(mmx2_avg_pu8(std::get<I * 2>(tuple), std::get<I * 2 + 1>(tuple)))...);
            else
                return simd_data<T>(mmx2_avg_pu8(std::get<0>(tuple), std::get<1>(tuple)));
        };

    public:
        template<simd, sample_data... Ds>
        auto operator()(format_nosimd, Ds... data) const
        {
            using T = type<Ds...>;
            if constexpr (std::integral<T>)
            {
                constexpr auto shift = this->shift<Ds...>;
                if constexpr (sizeof(T) < sizeof(int))
                {
                    using U = std::conditional_t<std::signed_integral<T>, int, unsigned>;
                    static_assert((sizeof(U) - sizeof(T)) * 8 >= shift);
                    return simd_data<T>((static_cast<U>(data) + ...) >> shift);
                }
                else return simd_data<T>(((data >> shift) + ...));
            }
            else return simd_data<T>((data + ...) * factor<Ds...>);
        }

        template<simd, sample_data... Ds> requires (all_same<Ds...>)
        auto operator()(format_ps, Ds... data) const
        {
            using T = type<Ds...>;
            return simd_data<T>(_mm_mul_ps(mix_ps(data...), _mm_set1_ps(factor<Ds...>)));
        }

        template<simd, sample_data... Ds> requires (all_same<Ds...>)
        auto operator()(format_pf, Ds... data) const
        {
            using T = type<Ds...>;
            constexpr auto f = reinterpret_cast<__m64>(simd_vector<float, 2> { factor<Ds...>, factor<Ds...> });
            return simd_data<T>(_m_pfmul(mix_pf(data...), f));
        }

        template<simd, sample_data... Ds>
        auto operator()(format_pi32, Ds... data) const
        {
            using T = type<Ds...>;
            constexpr auto headroom = this->headroom<format_pi16, Ds...>;
            constexpr auto shift = this->shift<Ds...>;
            if constexpr (shift < headroom)
            {
                const auto mix = mix_pi32_oversized(data...);
                if constexpr (std::unsigned_integral<T>)
                    return simd_data<T>(_mm_srli_pi32(mix, shift));
                else
                    return simd_data<T>(_mm_srai_pi32(mix, shift));
            }
            else return simd_data<T>(mix_pi32<T, shift>(data...));
        }

        template<simd flags, sample_data... Ds>
        auto operator()(format_pi16, Ds&&... data) const
        {
            using T = type<Ds...>;
            if constexpr (sizeof...(Ds) == 1) return simd_data<T>(data...);

            if constexpr (flags.match(simd::mmx2) and sizeof...(Ds) % 2 == 0 and std::unsigned_integral<T>)
            {
                auto do_mix = [&] { return mix_pu16<T>(std::forward_as_tuple(std::forward<Ds>(data)...), std::make_index_sequence<sizeof...(Ds) / 2> { }); };
                if constexpr (sizeof...(Ds) > 2)
                    return simd_apply<flags>(*this, do_mix());
                else
                    return do_mix();
            }

            constexpr auto headroom = this->headroom<format_pi16, Ds...>;
            constexpr auto shift = this->shift<Ds...>;
            if constexpr (shift < headroom)
            {
                const auto mix = mix_pi16_oversized(data...);
                if constexpr (std::unsigned_integral<T>)
                    return simd_data<T>(_mm_srli_pi16(mix, shift));
                else
                    return simd_data<T>(_mm_srai_pi16(mix, shift));
            }
            else return simd_data<T>(mix_pi16<T, shift>(data...));
        }

        template<simd flags, typename... Ds>
        requires (std::same_as<sample_u8, type<Ds...>> and flags.match(simd::mmx2) and std::has_single_bit(sizeof...(Ds)))
        auto operator()(format_pi8, Ds... data) const
        {
            using T = type<Ds...>;
            auto do_mix = [&] { return mix_pu8<T>(std::forward_as_tuple(std::forward<Ds>(data)...), std::make_index_sequence<sizeof...(Ds) / 2> { }); };
            if constexpr (sizeof...(Ds) > 2)
                return simd_apply<flags>(*this, do_mix());
            else
                return do_mix();
        }
    } inline constexpr sample_mix;
}
