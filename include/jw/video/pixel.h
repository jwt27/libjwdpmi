/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <mmintrin.h>
#include <xmmintrin.h>
#include <jw/common.h>
#include <jw/math.h>

namespace jw
{
    namespace video
    {
        namespace bios_colors
        {
            enum color : byte
            {
                black, blue, green, cyan, red, magenta, brown, light_gray,
                dark_gray, light_blue, light_green, light_cyan, light_red, pink, yellow, white
            };
        }

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

        struct alignas(0x10) bgra_ffff
        {
            using T = float;
            T b, g, r, a;

            static constexpr T rx = 1.0f;
            static constexpr T gx = 1.0f;
            static constexpr T bx = 1.0f;
            static constexpr T ax = 1.0f;
        };

        struct alignas(0x10) bgra_fff0
        {
            using T = float;
            T b, g, r, a;

            static constexpr T rx = 1.0f;
            static constexpr T gx = 1.0f;
            static constexpr T bx = 1.0f;
            static constexpr T ax = 0.0f;
        };

        struct alignas(4) bgra_8888
        {
            using T = std::uint8_t;
            T b, g, r, a;

            static constexpr T rx = 255;
            static constexpr T gx = 255;
            static constexpr T bx = 255;
            static constexpr T ax = 255;
        };

        struct [[gnu::packed]] bgra_8880
        {
            using T = std::uint8_t;
            T b, g, r, a;

            static constexpr T rx = 255;
            static constexpr T gx = 255;
            static constexpr T bx = 255;
            static constexpr T ax = 0;
        };

        struct [[gnu::packed]] bgr_8880
        {
            using T = std::uint8_t;
            T b, g, r;

            static constexpr T rx = 255;
            static constexpr T gx = 255;
            static constexpr T bx = 255;
            static constexpr T ax = 0;
        };

        struct [[gnu::packed]] bgra_6668
        {
            using T = unsigned;
            T b : 6, : 2;
            T g : 6, : 2;
            T r : 6, : 2;
            T a : 8;

            static constexpr T rx = 63;
            static constexpr T gx = 63;
            static constexpr T bx = 63;
            static constexpr T ax = 255;
        };

        struct alignas(2) [[gnu::packed]] bgr_5650
        {
            using T = unsigned;
            T b : 5;
            T g : 6;
            T r : 5;

            static constexpr T rx = 31;
            static constexpr T gx = 63;
            static constexpr T bx = 31;
            static constexpr T ax = 0;
        };

        struct alignas(2) [[gnu::packed]] bgra_5551
        {
            using T = unsigned;
            T b : 5;
            T g : 5;
            T r : 5;
            T a : 1;

            static constexpr T rx = 31;
            static constexpr T gx = 31;
            static constexpr T bx = 31;
            static constexpr T ax = 1;
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
        };

        struct alignas(2)[[gnu::packed]] bgra_4444
        {
            using T = unsigned;
            T b : 4;
            T g : 4;
            T r : 4;
            T a : 4;

            static constexpr T rx = 15;
            static constexpr T gx = 15;
            static constexpr T bx = 15;
            static constexpr T ax = 15;
        };

        struct [[gnu::packed]] bgr_3320
        {
            using T = unsigned;
            T b : 3;
            T g : 3;
            T r : 2;

            static constexpr T rx = 7;
            static constexpr T gx = 7;
            static constexpr T bx = 3;
            static constexpr T ax = 0;
        };

        struct [[gnu::packed]] bgra_2321
        {
            using T = unsigned;
            T b : 2;
            T g : 3;
            T r : 2;
            T a : 1;

            static constexpr T rx = 3;
            static constexpr T gx = 7;
            static constexpr T bx = 3;
            static constexpr T ax = 1;
        };

        struct[[gnu::packed]] bgra_2222
        {
            using T = unsigned;
            T b : 2;
            T g : 2;
            T r : 2;
            T a : 2;

            static constexpr T rx = 3;
            static constexpr T gx = 3;
            static constexpr T bx = 3;
            static constexpr T ax = 3;
        };

        struct [[gnu::packed]] px { };

        template<typename P>
        struct alignas(P) [[gnu::packed]] pixel : public P, public px
        {
            using T = typename P::T;

            template<std::size_t N, typename T>
            using V [[gnu::vector_size(N * sizeof(T))]] = T;

            template<typename T>
            union vector
            {
                using V [[gnu::vector_size(4 * sizeof(T))]] = T;
                V v;
                struct { T b, g, r, a; };
                constexpr vector(auto cr, auto cg, auto cb, auto ca) noexcept : b(cb), g(cg), r(cr), a(ca) { }
            };

            constexpr pixel() noexcept = default;
            template<typename U = P, std::enable_if_t<pixel<U>::has_alpha(), bool> = { }>
            constexpr pixel(T cr, T cg, T cb, T ca) noexcept : P { cb, cg, cr, ca } { }
            template<typename U = P, std::enable_if_t<pixel<U>::has_alpha(), bool> = { } >
            constexpr pixel(T cr, T cg, T cb) noexcept : P { cb, cg, cr, P::ax } { }
            template<typename U = P, std::enable_if_t<not pixel<U>::has_alpha(), bool> = { } >
            constexpr pixel(T cr, T cg, T cb, T) noexcept : P { cb, cg, cr } { }
            template<typename U = P, std::enable_if_t<not pixel<U>::has_alpha(), bool> = { } >
            constexpr pixel(T cr, T cg, T cb) noexcept : P { cb, cg, cr } { }

            constexpr pixel(const pixel& p) noexcept = default;
            constexpr pixel(pixel&& p) noexcept = default;

            template <typename U> constexpr operator pixel<U>() const noexcept { return cast_to<U>(); }
            template <typename U> constexpr pixel& operator=(const pixel<U>& other) noexcept { return *this = std::move(other.template cast_to<P>()); }
            template <typename U> constexpr pixel& operator=(pixel<U>&& other) noexcept { return *this = std::move(other.template cast_to<P>()); }
            constexpr pixel& operator=(const pixel& other) noexcept { return blend(other); }
            constexpr pixel& operator=(pixel&& o) noexcept = default;

            constexpr pixel& assign_round(const auto& v) noexcept
            { 
                if constexpr (std::is_integral_v<typename P::T>)
                {
                    this->b = jw::round(v.b);
                    this->g = jw::round(v.g);
                    this->r = jw::round(v.r);
                    if constexpr (has_alpha()) this->a = jw::round(v.a);
                    return *this;
                }
                this->b = v.b;
                this->g = v.g;
                this->r = v.r;
                if constexpr (has_alpha()) this->a = v.a;
                return *this;
            }

            static constexpr pixel m64(auto value) noexcept
            {
                auto v = reinterpret_cast<V<8, byte>>(_mm_packs_pu16(value, _mm_setzero_si64()));
                if constexpr (byte_aligned())
                {
                    pixel result { std::move(reinterpret_cast<pixel&&>(v)) };
                    _mm_empty();
                    return result;
                }
                else
                {
                    pixel result { v[2], v[1], v[0], v[3] };
                    _mm_empty();
                    return result;
                }
            }

            constexpr __m64 m64() const noexcept
            {
                auto ret = [] (auto v) { return _mm_unpacklo_pi8(v, _mm_setzero_si64()); };
                if constexpr (byte_aligned()) return ret(*reinterpret_cast<const __m64*>(this));
                else if constexpr (P::has_alpha) return ret(_mm_setr_pi8(this->b, this->g, this->r, this->a, 0, 0, 0, 0));
                else return ret(_mm_setr_pi8(this->b, this->g, this->r, 0, 0, 0, 0, 0));
            }

            static constexpr pixel m128(__m128 value) noexcept
            {
                if constexpr (std::is_floating_point_v<typename P::T>) return *reinterpret_cast<pixel*>(value);
                else return m64(_mm_cvtps_pi16(value));
            }

            constexpr __m128 m128() const noexcept
            {
                if constexpr (std::is_floating_point_v<typename P::T>) return *reinterpret_cast<const __m128*>(this);
                else return _mm_cvtpu16_ps(m64());
            }

            static constexpr auto vector_shift(std::uint16_t noalpha = 255) noexcept
            {
                return reinterpret_cast<__m64>((V<4, std::uint16_t> { P::bx, P::gx, P::rx, static_cast<std::uint16_t>(has_alpha() ? P::ax : noalpha) } + 1) / 8);
            }

            static constexpr auto vector_max(float noalpha = 1) noexcept
            {
                return reinterpret_cast<__m128>(V<4, float> { P::bx, P::gx, P::rx, has_alpha() ? P::ax : noalpha });
            }

            static constexpr bool byte_aligned()
            {
                return std::is_same_v<P, bgra_8888> or std::is_same_v<P, bgra_8880> or std::is_same_v<P, bgra_6668> or std::is_same_v<P, bgr_8880>;
            }

            static constexpr bool has_alpha() { return P::ax > 0; }

            template <typename U>
            constexpr pixel<U> cast_to() const noexcept
            {
                if constexpr (std::is_floating_point_v<typename P::T> or std::is_floating_point_v<typename U::T>)
                {
                    constexpr __m128 maxp = vector_max(1);
                    constexpr __m128 maxu = pixel<U>::vector_max(0);

                    __m128 src = m128();
                    src = _mm_div_ps(_mm_mul_ps(src, maxu), maxp);
                    return pixel<U>::m128(src);
                }
                else
                {
                    constexpr __m64 maxp = vector_shift(255);
                    constexpr __m64 maxu = pixel<U>::vector_shift(255);

                    __m64 src = m64();
                    src = _mm_srl_pi16(_mm_sll_pi16(src, maxu), maxp);
                    return pixel<U>::m64(src);
                }
            }

            template <typename U, std::enable_if_t<(std::is_integral<typename U::T>::value && std::is_integral<typename P::T>::value), bool> = { }> 
            static constexpr std::int16_t max(auto max) noexcept { return max + 1; }
            template <typename U, std::enable_if_t<(std::is_floating_point<typename U::T>::value || std::is_floating_point<typename P::T>::value), bool> = { }> 
            static constexpr float max(auto max) noexcept { return max; }

            template<typename V, typename U, std::enable_if_t<not pixel<U>::has_alpha(), bool> = { }>
            constexpr pixel& blend(const pixel<U>& src)
            {
                auto copy = src.template cast_to<V>();
                return assign_round<V>(copy);
            }

            template<typename V, typename U, std::enable_if_t<not pixel<V>::has_alpha() and pixel<U>::has_alpha(), bool> = { }>
            constexpr pixel& blend(const pixel<U>& other)
            {
                using max_T = decltype(max<U>(U::ax));

                if constexpr (std::is_integral<max_T>::value)
                {
                #ifndef __SSE__
                    this->b = ((other.b * max<U>(V::bx) * other.a) / max<U>(U::bx) + this->b * (other.ax - other.a)) / max<U>(U::ax);
                    this->g = ((other.g * max<U>(V::gx) * other.a) / max<U>(U::gx) + this->g * (other.ax - other.a)) / max<U>(U::ax);
                    this->r = ((other.r * max<U>(V::rx) * other.a) / max<U>(U::rx) + this->r * (other.ax - other.a)) / max<U>(U::ax);
                    return *this;
                #endif
                    using vec = vector<float>;
                    vec maxa { max<U>(U::ax), max<U>(U::ax), max<U>(U::ax), max<U>(U::ax) };
                    vec ax { U::ax, U::ax, U::ax, U::ax };
                    vec dest { this->r, this->g, this->b, 0 };
                    vec srca { other.a, other.a, other.a, other.a };
                    if constexpr (std::is_same<V, U>::value)
                    {
                        vec src { other.r, other.g, other.b, other.a };

                        src.v *= srca.v;
                        ax.v -= srca.v;
                        dest.v *= ax.v;
                        dest.v += src.v;
                        dest.v /= maxa.v;
                        return assign_round(dest);
                    }
                    else
                    {
                        vec maxu { max<U>(U::rx), max<U>(U::gx), max<U>(U::bx), max<U>(U::ax) };
                        vec src { other.r * max<U>(V::rx), other.g * max<U>(V::gx), other.b * max<U>(V::bx), other.a };

                        src.v *= srca.v;
                        src.v /= maxu.v;
                        ax.v -= srca.v;
                        dest.v *= ax.v;
                        dest.v += src.v;
                        dest.v /= maxa.v;
                        return assign_round(dest);
                    }
                }
                else
                {
                    using vec = vector<max_T>;
                    vec maxp { max<U>(V::rx), max<U>(V::gx), max<U>(V::bx), 0 };
                    vec maxu { max<U>(U::rx), max<U>(U::gx), max<U>(U::bx), max<U>(U::ax) };
                    vec maxa { max<U>(U::ax), max<U>(U::ax), max<U>(U::ax), max<U>(U::ax) };
                    vec ax { U::ax, U::ax, U::ax, U::ax };

                    vec src { other.r, other.g, other.b, other.a };
                    vec dest { this->r, this->g, this->b, 0 };
                    vec srca { src.a, src.a, src.a, src.a };

                    if constexpr (not std::is_same_v<V, U>) src.v *= maxp.v;
                    src.v *= srca.v;
                    if constexpr (not std::is_same_v<V, U>) src.v /= maxu.v;
                    ax.v -= srca.v;
                    dest.v *= ax.v;
                    dest.v += src.v;
                    dest.v /= maxa.v;
                    return assign_round(dest);
                }
            }

            template<typename V, typename U, std::enable_if_t<pixel<V>::has_alpha() and pixel<U>::has_alpha(), bool> = { }>
            constexpr pixel& blend(const pixel<U>& other)
            {
                using max_T = decltype(max<U>(U::ax));
                if constexpr (std::is_integral<max_T>::value)
                {
                #ifndef __SSE__
                    this->b = ((other.b * max<U>(V::bx) * other.a) / max<U>(U::bx) + this->b * (other.ax - other.a)) / max<U>(U::ax);
                    this->g = ((other.g * max<U>(V::gx) * other.a) / max<U>(U::gx) + this->g * (other.ax - other.a)) / max<U>(U::ax);
                    this->r = ((other.r * max<U>(V::rx) * other.a) / max<U>(U::rx) + this->r * (other.ax - other.a)) / max<U>(U::ax);
                    this->a = ((other.a * max<U>(V::ax) * other.a) / max<U>(U::ax) + this->a * (other.ax - other.a)) / max<U>(U::ax);
                    return *this;
                #endif
                    using vec = vector<float>;
                    vec maxa { max<U>(U::ax), max<U>(U::ax), max<U>(U::ax), max<U>(U::ax) };
                    vec ax { U::ax, U::ax, U::ax, U::ax };
                    vec dest { this->r, this->g, this->b, this->a };
                    vec srca { other.a, other.a, other.a, other.a };
                    if constexpr (std::is_same<P, U>::value)
                    {
                        vec src { other.r, other.g, other.b, other.a };

                        src.v *= srca.v;
                        ax.v -= srca.v;
                        dest.v *= ax.v;
                        dest.v += src.v;
                        dest.v /= maxa.v;
                        return assign_round(dest);
                    }
                    else
                    {
                        vec maxu { max<U>(U::rx), max<U>(U::gx), max<U>(U::bx), max<U>(U::ax) };
                        vec src { other.r * max<U>(V::rx), other.g * max<U>(V::gx), other.b * max<U>(V::bx), other.a * max<U>(V::ax) };

                        src.v *= srca.v;
                        src.v /= maxu.v;
                        ax.v -= srca.v;
                        dest.v *= ax.v;
                        dest.v += src.v;
                        dest.v /= maxa.v;
                        return assign_round(dest);
                    }
                }
                else
                {
                    using vec = vector<max_T>;
                    vec maxp { max<U>(V::rx), max<U>(V::gx), max<U>(V::bx), max<U>(V::ax) };
                    vec maxu { max<U>(U::rx), max<U>(U::gx), max<U>(U::bx), max<U>(U::ax) };
                    vec maxa { max<U>(U::ax), max<U>(U::ax), max<U>(U::ax), max<U>(U::ax) };
                    vec ax { U::ax, U::ax, U::ax, U::ax };

                    vec src { other.r, other.g, other.b, other.a };
                    vec dest { this->r, this->g, this->b, this->a };
                    vec srca { src.a, src.a, src.a, src.a };

                    if constexpr (not std::is_same_v<V, U>) src.v *= maxp.v;
                    src.v *= srca.v;
                    if constexpr (not std::is_same_v<V, U>) src.v /= maxu.v;
                    ax.v -= srca.v;
                    dest.v *= ax.v;
                    dest.v += src.v;
                    dest.v /= maxa.v;
                    return assign_round(dest);
                }
            }

            constexpr pixel& blend(const auto& other) { return blend<P>(other); }
        };

        struct [[gnu::packed]] px8
        {
            byte value { };

            constexpr px8() noexcept = default;
            constexpr px8(byte v) : value(v) { }
            constexpr px8& operator=(byte p) { value = (p == 0 ? value : p); return *this; }
            constexpr px8& operator=(const px8& p) { value = (p.value == 0 ? value : p.value); return *this; }
            constexpr operator byte() { return value; }

            template<typename T> constexpr auto cast(const auto& pal) { const auto& p = pal[value]; return T { p.r, p.g, p.b, p.a }; }
        };

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
        using px8n   = pixel<bgr_3320>;      // 8-bit 2:3:3, no alpha
        using pxvga  = pixel<bgra_6668>;     // VGA DAC palette format

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
    }
}
