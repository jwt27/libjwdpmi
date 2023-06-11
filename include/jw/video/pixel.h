#/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
#/*    Copyright (C) 2017 - 2023 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <type_traits>
#include <array>
#include <mmintrin.h>
#include <xmmintrin.h>
#include <jw/mmx.h>
#include <jw/math.h>
#include <jw/fixed.h>

namespace jw::video::bios_colors
{
    enum color : byte
    {
        black, blue, green, cyan, red, magenta, brown, light_gray,
        dark_gray, light_blue, light_green, light_cyan, light_red, pink, yellow, white
    };
}

namespace jw::video
{
    union [[gnu::packed]] text_attr
    {
        struct [[gnu::packed]]
        {
            unsigned foreground : 4;
            unsigned background : 3;
            bool blink : 1;
        };
        std::uint8_t raw_value;

        constexpr text_attr() noexcept : text_attr(7, 0, false) { }
        constexpr text_attr(byte fcol, byte bcol, bool _blink) noexcept : foreground(fcol), background(bcol), blink(_blink) { }
    };

    union alignas(2) [[gnu::packed]] text_char
    {
        struct [[gnu::packed]] char_with_attr
        {
            char character;
            text_attr attr;

            constexpr char_with_attr(char c, byte fcol, byte bcol, bool _blink) noexcept : character(c), attr(fcol, bcol, _blink) { }
            constexpr char_with_attr(char c, text_attr a) noexcept : character(c), attr(a) { }
            constexpr char_with_attr(char c) noexcept : character(c), attr() { }
        } value;
        std::uint16_t raw_value;

        constexpr text_char() noexcept : text_char(' ') { }
        constexpr text_char(char c, text_attr a) noexcept : value(c, a) { }
        constexpr text_char(char c, byte fcol = 7, byte bcol = 0, bool _blink = false) noexcept : text_char(c, { fcol, bcol, _blink }) { }
        constexpr explicit text_char(std::uint16_t v) noexcept : raw_value(v) { }
        constexpr explicit operator std::uint16_t() const noexcept{ return raw_value; }
        constexpr text_char& operator=(char c) noexcept { value.character = c; return *this; }
        constexpr operator char() const noexcept{ return value.character; }
    };
    static_assert(sizeof(text_char) == 2 && alignof(text_char) == 2, "text_char has incorrect size or alignment.");

    template<typename T>
    concept pixel_layout = requires (T layout)
    {
        // Data type (float or unsigned)
        typename T::T;

        // Maximum value for each component
        { T::rx }; { T::gx }; { T::bx }; { T::ax };

        // Color components
        { layout.r } -> std::same_as<typename T::T&>;
        { layout.g } -> std::same_as<typename T::T&>;
        { layout.b } -> std::same_as<typename T::T&>;

        // Optional alpha component
        requires T::ax == 0 or requires { { layout.a } -> std::same_as<typename T::T&>; };

        // Specifies if component bit-fields are aligned on byte boundaries
        // (enables efficient conversion to/from __m64)
        { T::byte_aligned } -> std::convertible_to<bool>;

        // Max values must be a power of two, minus one.
        requires (std::floating_point<typename T::T> or
                  (std::has_single_bit(static_cast<unsigned>(T::rx + 1)) and
                   std::has_single_bit(static_cast<unsigned>(T::gx + 1)) and
                   std::has_single_bit(static_cast<unsigned>(T::bx + 1)) and
                   std::has_single_bit(static_cast<unsigned>(T::ax + 1))));
    };

    template<pixel_layout P>
    struct alignas(P) [[gnu::packed, gnu::may_alias]] pixel : P
    {
        template<pixel_layout> friend struct pixel;

        using layout = P;
        using T = typename layout::T;

        static consteval bool has_alpha() { return P::ax > 0; }

        static constexpr pixel rgba(auto r, auto g, auto b, auto a) noexcept
        {
            pixel px;
            px.b = static_cast<T>(b);
            px.g = static_cast<T>(g);
            px.r = static_cast<T>(r);
            if constexpr (has_alpha()) px.a = static_cast<T>(a);
            return px;
        }

        static constexpr pixel rgb(auto r, auto g, auto b) noexcept
        {
            return rgba(r, g, b, P::ax);
        }

        static consteval T component_max(bool with_alpha) noexcept
        {
            T x = std::max({ P::bx, P::gx, P::rx });
            if (has_alpha() and with_alpha) x = std::max(x, P::ax);
            return x;
        }

        static consteval T component_min(bool with_alpha) noexcept
        {
            T x = std::min({ P::bx, P::gx, P::rx });
            if (has_alpha() and with_alpha) x = std::min(x, P::ax);
            return x;
        }

        static constexpr std::array<T, 4> max { P::bx, P::gx, P::rx, P::ax };

        static consteval bool byte_aligned() noexcept { return P::byte_aligned; }
    };

    template<typename P>
    concept pixel_type = requires { typename P::layout; } and std::same_as<P, pixel<typename P::layout>>;

    template<typename P>
    concept pixel_data = pixel_type<simd_type<P>>;

    template<typename T>
    union pixel_proxy
    {
        using type = T;
        struct { T b, g, r, a; };
        std::array<T, 4> array;
        simd_vector<T, 4> vector;

        template<typename U>
        constexpr operator pixel_proxy<U>() const noexcept
        {
            pixel_proxy<U> px;
            px.b = b;
            px.g = g;
            px.r = r;
            px.a = a;
            return px;
        }
    };

    template<pixel_type... P>
    using pixel_proxy_for = pixel_proxy<std::conditional_t<(std::floating_point<typename P::T> or ...), float, int>>;
}

namespace jw::video::px_utils
{
    template<float factor, any_of<int, float> T>
    constexpr inline T multiply_float(T x) noexcept
    {
        if constexpr (std::floating_point<T>) return x *= factor;
        else
        {
            constexpr fixed<int, 16> ff { factor };
            auto v = ff;
            v *= x;
            return round(v);
        }
    }
}

namespace jw
{
    template<typename P>
    struct simd_type_traits<video::pixel<P>, format_nosimd>
    {
        using data_type = video::pixel_proxy_for<video::pixel<P>>;
        static constexpr std::size_t delta = 1;
    };

    template<typename P> requires (not std::floating_point<typename P::T>)
    struct simd_type_traits<video::pixel<P>, format_pi8>
    {
        using data_type = m64_t;
        static constexpr std::size_t delta = 2;
    };

    template<typename P> requires (not std::floating_point<typename P::T>)
    struct simd_type_traits<video::pixel<P>, format_pi16>
    {
        using data_type = m64_t;
        static constexpr std::size_t delta = 1;
    };

    template<typename P>
    struct simd_type_traits<video::pixel<P>, format_ps>
    {
        using data_type = m128_t;
        static constexpr std::size_t delta = 1;
    };

    template<simd flags, std::indirectly_readable I> requires (video::pixel_type<std::iter_value_t<I>>)
    [[gnu::always_inline]] inline auto simd_load(format_nosimd, I src)
    {
        using P = std::iter_value_t<I>;
        auto px = *src;
        video::pixel_proxy_for<P> proxy;
        proxy.b = px.b;
        proxy.g = px.g;
        proxy.r = px.r;
        if constexpr (P::has_alpha()) proxy.a = px.a;
        return proxy;
    }

    template<simd flags, std::random_access_iterator I>
    requires (video::pixel_type<std::iter_value_t<I>> and std::integral<typename std::iter_value_t<I>::T>)
    [[gnu::always_inline]] inline auto simd_load(format_pi8, I src)
    {
        using P = std::iter_value_t<I>;
        __m64 v;
        if constexpr (P::byte_aligned())
        {
            if constexpr (sizeof(P) == 4 and std::contiguous_iterator<I>)
                asm ("movq %0, %1" : "=y" (v) : "m" (*src));
            else
            {
                __m64 lo = *reinterpret_cast<const __m64*>(&src[0]);
                __m64 hi = *reinterpret_cast<const __m64*>(&src[1]);
                v = _mm_unpacklo_pi32(lo, hi);
            }
        }
        else
        {
            auto make = [](auto px)
            {
                __m64 v;
                std::uint8_t a[4];
                a[0] = px.b;
                a[1] = px.g;
                a[2] = px.r;
                if constexpr (P::has_alpha()) a[4] = px.a;
                auto b = *reinterpret_cast<std::uint32_t*>(a);
                asm ("movd %0, %1" : "=y" (v) : "rm" (b));
                return v;
            };
            auto lo = make(src[0]);
            auto hi = make(src[1]);
            v = _mm_unpacklo_pi32(lo, hi);
        }
        return v;
    }

    template<simd flags, std::indirectly_readable I>
    requires (video::pixel_type<std::iter_value_t<I>> and std::integral<typename std::iter_value_t<I>::T>)
    [[gnu::always_inline]] inline auto simd_load(format_pi16, I src)
    {
        using P = std::iter_value_t<I>;
        __m64 v;
        if constexpr (P::byte_aligned()) asm ("movd %0, %1" : "=y" (v) : "rm" (*src));
        else
        {
            auto px = *src;
            std::uint8_t a[4];
            a[0] = px.b;
            a[1] = px.g;
            a[2] = px.r;
            if constexpr (P::has_alpha()) a[3] = px.a;
            auto b = *reinterpret_cast<std::uint32_t*>(a);
            asm ("movd %0, %1" : "=y" (v) : "rm" (b));
        }
        v = _mm_unpacklo_pi8(v, _mm_setzero_si64());
        return v;
    }

    template<simd flags, std::indirectly_readable I> requires (video::pixel_type<std::iter_value_t<I>>)
    [[gnu::always_inline]] inline auto simd_load(format_ps, I src)
    {
        using P = std::iter_value_t<I>;
        if constexpr (std::floating_point<typename P::T>) return *reinterpret_cast<const __m128*>(&*src);
        else
        {
            auto m64 = simd_load<flags>(pi16, src);
            return _mm_cvtpu16_ps(m64);
        }
    }

    template<simd flags, typename I> requires (video::pixel_type<std::iter_value_t<I>>)
    [[gnu::always_inline]] inline void simd_store(format_nosimd, I dst, video::pixel_proxy_for<std::iter_value_t<I>> src)
    {
        using P = std::iter_value_t<I>;
        P px;
        px.b = src.b;
        px.g = src.g;
        px.r = src.r;
        if constexpr (P::has_alpha()) px.a = src.a;
        *dst = std::move(px);
    }

    template<simd flags, std::random_access_iterator I> requires (video::pixel_type<std::iter_value_t<I>> and std::integral<typename std::iter_value_t<I>::T>)
    [[gnu::always_inline]] inline void simd_store(format_pi8, I dst, __m64 src)
    {
        using P = std::iter_value_t<I>;
        if constexpr (P::byte_aligned() and sizeof(P) == 4)
        {
            if constexpr (std::contiguous_iterator<I>)
                asm ("movq %0, %1" : "=m" (*dst) : "y" (src));
            else
            {
                P px[2];
                *reinterpret_cast<__m64*>(px) = src;
                dst[0] = std::move(px[0]);
                dst[1] = std::move(px[1]);
            }
        }
        else
        {
            auto make = [](const std::uint8_t* a)
            {
                P px;
                px.b = a[0];
                px.g = a[1];
                px.r = a[2];
                if constexpr (P::has_alpha()) px.a = a[3];
            };
            const auto* const a = reinterpret_cast<const std::uint8_t*>(&src);
            dst[0] = make(a + 0);
            dst[1] = make(a + 4);
        }
    }

    template<simd flags, typename I> requires (video::pixel_type<std::iter_value_t<I>> and std::integral<typename std::iter_value_t<I>::T>)
    [[gnu::always_inline]] inline void simd_store(format_pi16, I dst, __m64 src)
    {
        using P = std::iter_value_t<I>;
        const __m64 v = _mm_packs_pu16(src, src);
        if constexpr (P::byte_aligned() and sizeof(P) == 4) asm ("movd %0, %1" : "=rm" (*dst) : "y" (v));
        else
        {
            P px;
            const auto* const v2 = reinterpret_cast<const std::uint8_t*>(&v);
            px.b = v2[0];
            px.g = v2[1];
            px.r = v2[2];
            if constexpr (P::has_alpha()) px.a = v2[3];
            *dst = std::move(px);
        }
    }

    template<simd flags, typename I> requires (video::pixel_type<std::iter_value_t<I>>)
    [[gnu::always_inline]] inline void simd_store(format_ps, I dst, __m128 src)
    {
        using P = std::iter_value_t<I>;
        __m128 v = src;
        if constexpr (std::floating_point<typename P::T>) *dst = *reinterpret_cast<P*>(&v);
        else simd_store(pi16, dst, _mm_cvtps_pi16(v));
    }
}

namespace jw::video
{
    // Convert between pixel layouts.
    template<pixel_type Dst>
    struct px_convert_t
    {
        template<simd flags, simd_data_for<Dst> D>
        auto operator()(simd_format auto, D dsrc) const
        {
            return dsrc;
        }

        template<simd flags, any_simd_format_of<format_nosimd, format_pi16, format_ps> F, pixel_data D>
        auto operator()(F, D dsrc) const
        {
            using Src = simd_type<D>;
            using proxy = pixel_proxy_for<Dst, Src>;
            using VT = proxy::type;

            constexpr bool convert_alpha = Src::has_alpha() and Dst::has_alpha();
            constexpr bool insert_alpha = Dst::has_alpha() and not Src::has_alpha();
            constexpr std::array<VT, 4> mul { Dst::bx, Dst::gx, Dst::rx, convert_alpha ? Dst::ax : 0 };
            constexpr std::array<VT, 4> div { Src::bx, Src::gx, Src::rx, convert_alpha ? Src::ax : 1 };

            if constexpr (std::same_as<F, format_nosimd>)
            {
                proxy dst = dsrc;
                auto cvt = [&]<std::size_t I>()
                {
                    if constexpr (std::floating_point<VT> or Dst::max[I] > Src::max[I])
                    {
                        constexpr auto factor = static_cast<float>(mul[I]) / div[I];
                        dst.array[I] = px_utils::multiply_float<factor>(dst.array[I]);
                    }
                    else
                    {
                        constexpr int dbits = std::bit_width(static_cast<unsigned>(Dst::max[I]));
                        constexpr int sbits = std::bit_width(static_cast<unsigned>(Src::max[I]));
                        dst.array[I] >>= sbits - dbits;
                    }
                };

                auto loop = [cvt]<std::size_t... I>(std::index_sequence<I...>)
                {
                    (cvt.template operator()<I>(), ...);
                };
                loop(std::make_index_sequence<3 + convert_alpha> { });

                if constexpr (insert_alpha)
                    dst.a = Dst::ax;
                return simd_data<Dst>(dst);
            }
            else if constexpr (std::same_as<F, format_pi16>)
            {
                constexpr bool src_equal = Src::bx == Src::gx and Src::bx == Src::rx and (Src::bx == Src::ax or not convert_alpha);
                constexpr bool dst_equal = Dst::bx == Dst::gx and Dst::bx == Dst::rx and (Dst::bx == Dst::ax or not convert_alpha);
                constexpr bool can_shift = src_equal and dst_equal and Src::bx >= Dst::bx;

                __m64 dst;

                if constexpr (can_shift)
                {
                    constexpr int dbits = std::bit_width(static_cast<unsigned>(Dst::bx));
                    constexpr int sbits = std::bit_width(static_cast<unsigned>(Src::bx));
                    dst = _mm_srli_pi16(dsrc, sbits - dbits);
                }
                else
                {
                    constexpr auto src_max = Src::component_max(convert_alpha);
                    dst = mmx_muldiv_pu16<flags, true, mul, div, src_max>(dsrc);
                }

                if constexpr (insert_alpha)
                    dst = _mm_or_si64(dst, _mm_setr_pi16(0, 0, 0, Dst::ax));

                return simd_data<Dst>(dst);
            }
            else if constexpr (std::same_as<F, format_ps>)
            {
                constexpr __m128 factor = simd_vector<float, 4>
                {
                    mul[0] / div[0],
                    mul[1] / div[1],
                    mul[2] / div[2],
                    mul[3] / div[3]
                };
                __m128 dst = _mm_mul_ps(dsrc, factor);
                if constexpr (insert_alpha)
                    dst[3] = Dst::ax;
                return simd_data<Dst>(dst);
            }
        }
    };

    template<pixel_type Dst>
    inline constexpr px_convert_t<Dst> px_convert;

    // Multiply color components by alpha.
    struct px_premultiply_alpha_t
    {
        template<simd flags, pixel_data D>
        auto operator()(format_nosimd, D dsrc) const
        {
            using P = simd_type<D>;
            if constexpr (not P::has_alpha()) return dsrc;
            auto dst = dsrc;
            const auto a = dst.a;
            for (unsigned i = 0; i < 3; ++i)
            {
                dst.array[i] *= a;
                if constexpr (P::ax != 1)
                    dst.array[i] = px_utils::multiply_float<1.f / P::ax>(dst.array[i]);
            }
            return dst;
        }

        template<simd flags, pixel_data D>
        auto operator()(format_pi16, D dsrc) const
        {
            using P = simd_type<D>;
            if constexpr (not P::has_alpha()) return dsrc;
            constexpr std::uint16_t max = P::component_max(true);
            const __m64 tmp = mmx_insert_pi16<flags, 0>(dsrc, P::ax);
            const __m64 va = mmx_shuffle_pi16<flags, shuffle_mask { 3, 3, 3, 0 }>(tmp);
            auto dst = _mm_mullo_pi16(dsrc, va);
            dst = mmx_div_scalar_pu16<flags, true, P::ax, max * P::ax>(dst);
            return dst;
        }

        template<simd flags, pixel_data D>
        auto operator()(format_ps, D dsrc) const
        {
            using P = simd_type<D>;
            if constexpr (not P::has_alpha()) return dsrc;
            auto va = _mm_move_ss(dsrc, _mm_set_ss(1));
            va = _mm_shuffle_ps(va, va, shuffle_mask { 3, 3, 3, 0 });
            auto dst = _mm_mul_ps(dsrc, va);
            if constexpr (P::ax != 1)
            {
                constexpr __m128 ax = 1.f / simd_vector<float, 4> { P::ax, P::ax, P::ax, 1 };
                dst = _mm_mul_ps(dst, ax);
            }
            return dst;
        }
    } inline constexpr px_premultiply_alpha;

    // Given inputs (dst, src), blend src over dst using straight alpha.
    struct px_blend_straight_t
    {
        template<simd flags, pixel_data DD, pixel_data DS>
        auto operator()(format_nosimd, DD ddst, DS dsrc) const
        {
            using Dst = simd_type<DD>;
            using Src = simd_type<DS>;
            using proxy = pixel_proxy_for<Dst, Src>;
            proxy dst = ddst;
            proxy src = dsrc;
            const auto a = src.a;

            if constexpr (not std::same_as<Dst, Src>)
                src = simd_invoke<flags>(px_convert<Dst>, nosimd, dsrc);

            for (unsigned i = 0; i < 3; ++i)
            {
                src.array[i] -= dst.array[i];
                src.array[i] *= a;
                src.array[i] = px_utils::multiply_float<1.f / Src::ax>(src.array[i]);
                dst.array[i] += src.array[i];
            }
            return simd_data<Dst>(dst);
        }

        template<simd flags, pixel_data DD, pixel_data DS>
        auto operator()(format_pi16, DD ddst, DS dsrc) const
        {
            using Dst = simd_type<DD>;
            using Src = simd_type<DS>;
            __m64 dst = ddst;
            __m64 src = dsrc;

            constexpr unsigned max = Dst::component_max(false);
            const int sa = mmx_extract_pi16<flags, 3>(dsrc);

            if constexpr (not std::same_as<Dst, Src>)
                src = simd_invoke<flags>(px_convert<Dst>, pi16, dsrc);

            src = _mm_mullo_pi16(src, _mm_set1_pi16(sa));
            dst = _mm_mullo_pi16(dst, _mm_set1_pi16(Src::ax - sa));

            constexpr std::array<int, 4> mul { 1, 1, 1, 0 };
            constexpr std::array<int, 4> div { Src::ax, Src::ax, Src::ax, 1 };
            src = mmx_muldiv_pu16<flags, true, mul, div, max * Src::ax>(src);
            dst = mmx_muldiv_pu16<flags, true, mul, div, max * Src::ax>(dst);

            dst = _mm_adds_pu8(dst, src);

            if constexpr (Dst::has_alpha())
            {
                const __m64 da = _mm_and_si64(ddst, _mm_setr_pi16(0, 0, 0, -1));
                dst = _mm_or_si64(dst, da);
            }
            return simd_data<Dst>(dst);
        }

        template<simd flags, pixel_data DD, pixel_data DS>
        auto operator()(format_ps, DD ddst, DS dsrc) const
        {
            using Dst = simd_type<DD>;
            using Src = simd_type<DS>;
            __m128 dst = ddst;
            __m128 src = dsrc;

            __m128 a = _mm_move_ss(src, _mm_set_ss(0));
            a = _mm_shuffle_ps(a, a, shuffle_mask { 3, 3, 3, 0 });

            if constexpr (not std::same_as<Dst, Src>)
                src = simd_invoke<flags>(px_convert<Dst>, ps, dsrc);

            src = _mm_sub_ps(src, dst);
            src = _mm_mul_ps(src, a);
            if constexpr (Src::ax != 1)
            {
                constexpr __m128 ax = 1.f / simd_vector<float, 4> { Src::ax, Src::ax, Src::ax, 1 };
                src = _mm_mul_ps(src, ax);
            }
            dst = _mm_add_ps(dst, src);
            return simd_data<Dst>(dst);
        }
    } inline constexpr px_blend_straight;

    // Given inputs (dst, src), blend src over dst using premultiplied alpha.
    // Does not saturate, so must be followed by px_clamp.
    struct px_blend_premultiplied_t
    {
        template<simd flags, pixel_data DD, pixel_data DS>
        auto operator()(format_nosimd, DD ddst, DS dsrc) const
        {
            using Dst = simd_type<DD>;
            using Src = simd_type<DS>;
            using proxy = pixel_proxy_for<Dst, Src>;
            proxy dst = ddst;
            proxy src = dsrc;
            const auto a = Src::ax - src.a;

            if constexpr (not std::same_as<Dst, Src>)
                src = simd_invoke<flags>(px_convert<Dst>, nosimd, dsrc);

            for (unsigned i = 0; i < 4; ++i)
            {
                dst.array[i] *= a;
                if constexpr (Src::ax != 1)
                    dst.array[i] = px_utils::multiply_float<1.f / Src::ax>(dst.array[i]);
                dst.array[i] += src.array[i];
            }
            return simd_data<Dst>(dst);
        }

        template<simd flags, pixel_data DD, pixel_data DS>
        auto operator()(format_pi16, DD ddst, DS dsrc) const
        {
            using Dst = simd_type<DD>;
            using Src = simd_type<DS>;
            __m64 dst = ddst;
            __m64 src = dsrc;

            constexpr std::uint16_t max = Dst::component_max(false);
            const int a = Src::ax - mmx_extract_pi16<flags, 3>(src);

            if constexpr (not std::same_as<Dst, Src>)
                src = simd_invoke<flags>(px_convert<Dst>, pi16, dsrc);

            dst = _mm_mullo_pi16(dst, _mm_set1_pi16(a));
            dst = mmx_div_scalar_pu16<flags, true, Src::ax, max * Src::ax>(dst);
            dst = _mm_adds_pi16(dst, src);

            return simd_data<Dst>(dst);
        }

        template<simd flags, pixel_data DD, pixel_data DS>
        auto operator()(format_ps, DD ddst, DS dsrc) const
        {
            using Dst = simd_type<DD>;
            using Src = simd_type<DS>;
            __m128 dst = ddst;
            __m128 src = dsrc;

            __m128 a = _mm_shuffle_ps(src, src, shuffle_mask { 3, 1, 2, 3 });
            a = _mm_sub_ss(_mm_set_ss(Src::ax), a);
            a = _mm_shuffle_ps(a, a, shuffle_mask { 0, 0, 0, 0 });

            if constexpr (not std::same_as<Dst, Src>)
                src = simd_invoke<flags>(px_convert<Dst>, ps, dsrc);

            dst = _mm_mul_ps(dst, a);
            if constexpr (Src::ax != 1)
            {
                constexpr __m128 ax = 1.f / simd_vector<float, 4> { Src::ax, Src::ax, Src::ax, Src::ax };
                dst = _mm_mul_ps(dst, ax);
            }
            dst = _mm_add_ps(dst, src);
            return simd_data<Dst>(dst);
        }
    } inline constexpr px_blend_premultiplied;

    // Clamp component levels to maximum values allowed by layout.
    struct px_clamp_t
    {
        template<simd, pixel_data D>
        auto operator()(format_nosimd, D dsrc) const
        {
            using P = simd_type<D>;
            using proxy = pixel_proxy_for<P>;
            proxy dst = dsrc;
            using T = proxy::type;
            for (unsigned i = 0; i < 3 + P::has_alpha(); ++i)
                dst.array[i] = std::clamp(dst.array[i], static_cast<T>(0), static_cast<T>(P::max[i]));
            return dst;
        }

        template<simd flags, pixel_data D> requires (flags.match(simd::mmx2))
        auto operator()(format_pi8, D dsrc) const
        {
            using P = simd_type<D>;
            constexpr auto max = reinterpret_cast<__m64>(simd_vector<std::uint8_t, 8>
            {
                P::bx, P::gx, P::rx, P::ax,
                P::bx, P::gx, P::rx, P::ax
            });

            if constexpr (P::bx == 0xff and P::gx == 0xff and P::rx == 0xff and (P::ax == 0xff or not P::has_alpha()))
                return dsrc;

            return mmx2_min_pu8(dsrc, max);
        }

        template<simd flags, pixel_data D>
        auto operator()(format_pi16, D dsrc) const
        {
            using P = simd_type<D>;
            constexpr auto max = reinterpret_cast<__m64>(simd_vector<std::uint16_t, 4> { P::bx, P::gx, P::rx, P::ax });

            if constexpr (flags.match(simd::mmx2))
            {
                __m64 dst = mmx2_max_pi16(dsrc, _mm_setzero_si64());
                dst = mmx2_min_pi16(dst, max);
                return dst;
            }
            else
            {
                const __m64 not_hi = _mm_cmpgt_pi16(max, dsrc);
                const __m64 not_lo = _mm_cmpgt_pi16(dsrc, _mm_setzero_si64());
                __m64 dst = _mm_and_si64(dsrc, not_hi);
                dst = _mm_and_si64(dst, not_lo);
                dst = _mm_or_si64(dst, _mm_andnot_si64(not_hi, max));
                return dst;
            }
        }

        template<simd, pixel_data D>
        auto operator()(format_ps, D dsrc) const
        {
            using P = simd_type<D>;
            constexpr __m128 max { P::bx, P::gx, P::rx, P::ax };

            __m128 dst = _mm_max_ps(dsrc, _mm_setzero_ps());
            dst = _mm_min_ps(dst, max);

            return dst;
        }
    } inline constexpr px_clamp;

    template<std::size_t bbits, std::size_t gbits, std::size_t rbits, std::size_t abits, std::size_t align = 1, bool is_byte_aligned = false>
    struct alignas(align) [[gnu::packed]] bgra
    {
        using T = unsigned;
        T b : bbits;
        T g : gbits;
        T r : rbits;
        T a : abits;

        static constexpr T rx = (1 << rbits) - 1;
        static constexpr T gx = (1 << gbits) - 1;
        static constexpr T bx = (1 << bbits) - 1;
        static constexpr T ax = (1 << abits) - 1;
        static constexpr bool byte_aligned = is_byte_aligned;
    };

    template<std::size_t bbits, std::size_t gbits, std::size_t rbits, std::size_t align = 1, bool is_byte_aligned = false>
    struct alignas(align) [[gnu::packed]] bgr
    {
        using T = unsigned;
        T b : bbits;
        T g : gbits;
        T r : rbits;

        static constexpr T rx = (1 << rbits) - 1;
        static constexpr T gx = (1 << gbits) - 1;
        static constexpr T bx = (1 << bbits) - 1;
        static constexpr T ax = 0;
        static constexpr bool byte_aligned = is_byte_aligned;
    };

    struct alignas(0x10) bgra_ffff
    {
        using T = float;
        T b, g, r, a;

        static constexpr T rx = 1.0f;
        static constexpr T gx = 1.0f;
        static constexpr T bx = 1.0f;
        static constexpr T ax = 1.0f;
        static constexpr bool byte_aligned = false;
    };

    struct alignas(0x10) bgra_fff0
    {
        using T = float;
        T b, g, r;
        unsigned : sizeof(float);

        static constexpr T rx = 1.0f;
        static constexpr T gx = 1.0f;
        static constexpr T bx = 1.0f;
        static constexpr T ax = 0.0f;
        static constexpr bool byte_aligned = false;
    };

    struct [[gnu::packed]] alignas(4) bgra_8880
    {
        using T = std::uint8_t;
        T b, g, r;
        T : 8;

        static constexpr T rx = 255;
        static constexpr T gx = 255;
        static constexpr T bx = 255;
        static constexpr T ax = 0;
        static constexpr bool byte_aligned = true;
    };

    struct [[gnu::packed]] bgra_6660
    {
        using T = unsigned;
        T b : 8;
        T g : 8;
        T r : 8;
        T : 8;

        static constexpr T rx = 63;
        static constexpr T gx = 63;
        static constexpr T bx = 63;
        static constexpr T ax = 0;
        static constexpr bool byte_aligned = true;
    };

    struct alignas(2) [[gnu::packed]] bgra_5550
    {
        using T = unsigned;
        T b : 5;
        T g : 5;
        T r : 5;
        T : 1;

        static constexpr T rx = 31;
        static constexpr T gx = 31;
        static constexpr T bx = 31;
        static constexpr T ax = 0;
        static constexpr bool byte_aligned = false;
    };

    using bgra_8888 = bgra<8, 8, 8, 8, 4, true>;
    using bgra_5551 = bgra<5, 5, 5, 1, 2, false>;
    using bgra_4444 = bgra<4, 4, 4, 4, 2, false>;
    using bgra_2222 = bgra<2, 2, 2, 2, 1, false>;
    using bgra_2321 = bgra<2, 3, 2, 1, 1, false>;

    using bgr_8880 = bgr<8, 8, 8, 1, true>;
    using bgr_5650 = bgr<5, 6, 5, 2, false>;
    using bgr_2330 = bgr<2, 3, 3, 1, false>;

    using pxf    = pixel<bgra_ffff>;     // floating-point for use with SSE
    using pxfn   = pixel<bgra_fff0>;     // floating-point, no alpha
    using px32a  = pixel<bgra_8888>;     // 24-bit, 8-bit alpha channel
    using px32n  = pixel<bgra_8880>;     // 24-bit, no alpha, 4 bytes wide
    using px24   = pixel<bgr_8880>;      // 24-bit, 3 bytes wide
    using px16   = pixel<bgr_5650>;      // 16-bit, typical 5:6:5 format
    using px16a  = pixel<bgra_5551>;     // 15-bit with 1-bit alpha
    using px16n  = pixel<bgra_5550>;     // 15-bit, no alpha, equal 5:5:5 format
    using px16aa = pixel<bgra_4444>;     // 12-bit, 4-bit alpha, equal 4:4:4 format
    using px8aa  = pixel<bgra_2222>;     // 6-bit 2:2:2, 2-bit alpha
    using px8a   = pixel<bgra_2321>;     // 7-bit 2:3:2, 1-bit alpha
    using px8n   = pixel<bgr_2330>;      // 8-bit 3:3:2, no alpha
    using pxvga  = pixel<bgra_6660>;     // VGA DAC palette format

    static_assert(sizeof(pxf   ) == 16);
    static_assert(sizeof(pxfn  ) == 16);
    static_assert(sizeof(px32a ) ==  4);
    static_assert(sizeof(px32n ) ==  4);
    static_assert(sizeof(px24  ) ==  3);
    static_assert(sizeof(px16  ) ==  2);
    static_assert(sizeof(px16aa) ==  2);
    static_assert(sizeof(px16a ) ==  2);
    static_assert(sizeof(px16n ) ==  2);
    static_assert(sizeof(px8aa ) ==  1);
    static_assert(sizeof(px8a  ) ==  1);
    static_assert(sizeof(px8n  ) ==  1);
    static_assert(sizeof(pxvga ) ==  4);

    template<simd flags>
    inline auto generate_px8n_palette() noexcept
    {
        std::array<px32n, 256> result;
        simd_pipeline pipe { simd_in, px_convert<px32n>, simd_out };
        for (unsigned i = 0; i < 256; ++i)
            result[i] = simd_run<default_simd()>(pipe, std::bit_cast<px8n>(static_cast<std::uint8_t>(i)));
        return result;
    }
}
