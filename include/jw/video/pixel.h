/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
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

            bgra_ffff() noexcept = default;
            constexpr bgra_ffff(T cr, T cg, T cb, T ca) noexcept : b(cb), g(cg), r(cr), a(ca) { }

            static constexpr T rx = 1.0f;
            static constexpr T gx = 1.0f;
            static constexpr T bx = 1.0f;
            static constexpr T ax = 1.0f;
            static constexpr bool has_alpha = true;
        };

        struct alignas(4) bgra_8888
        {
            using T = std::uint8_t;
            T b, g, r, a;

            bgra_8888() noexcept = default;
            constexpr bgra_8888(T cr, T cg, T cb, T ca) noexcept : b(cb), g(cg), r(cr), a(ca) { }

            static constexpr T rx = 255;
            static constexpr T gx = 255;
            static constexpr T bx = 255;
            static constexpr T ax = 255;
            static constexpr bool has_alpha = true;
        };

        struct [[gnu::packed]] bgra_8880
        {
            using T = std::uint8_t;
            T b, g, r, a;

            bgra_8880() noexcept = default;
            constexpr bgra_8880(T cr, T cg, T cb, T) noexcept : b(cb), g(cg), r(cr), a(0) { }

            static constexpr T rx = 255;
            static constexpr T gx = 255;
            static constexpr T bx = 255;
            static constexpr T ax = 0;
            static constexpr bool has_alpha = false;
        };

        struct [[gnu::packed]] bgr_8880
        {
            using T = std::uint8_t;
            T b, g, r;

            bgr_8880() noexcept = default;
            constexpr bgr_8880(T cr, T cg, T cb, T) noexcept : b(cb), g(cg), r(cr) { }

            static constexpr T rx = 255;
            static constexpr T gx = 255;
            static constexpr T bx = 255;
            static constexpr T ax = 0;
            static constexpr bool has_alpha = false;
        };

        struct [[gnu::packed]] bgra_6668
        {
            using T = unsigned;
            T b : 6, : 2;
            T g : 6, : 2;
            T r : 6, : 2;
            T a : 8;

            bgra_6668() noexcept = default;
            constexpr bgra_6668(T cr, T cg, T cb, T ca) noexcept : b(cb), g(cg), r(cr), a(ca) { }

            static constexpr T rx = 63;
            static constexpr T gx = 63;
            static constexpr T bx = 63;
            static constexpr T ax = 255;
            static constexpr bool has_alpha = true;
        };

        struct alignas(2) [[gnu::packed]] bgr_5650
        {
            using T = unsigned;
            T b : 5;
            T g : 6;
            T r : 5;

            bgr_5650() noexcept = default;
            constexpr bgr_5650(T cr, T cg, T cb, T) noexcept : b(cb), g(cg), r(cr) { }

            static constexpr T rx = 31;
            static constexpr T gx = 63;
            static constexpr T bx = 31;
            static constexpr T ax = 0;
            static constexpr bool has_alpha = false;
        };

        struct alignas(2) [[gnu::packed]] bgra_5551
        {
            using T = unsigned;
            T b : 5;
            T g : 5;
            T r : 5;
            T a : 1;

            bgra_5551() noexcept = default;
            constexpr bgra_5551(T cr, T cg, T cb, T ca) noexcept : b(cb), g(cg), r(cr), a(ca) { }

            static constexpr T rx = 31;
            static constexpr T gx = 31;
            static constexpr T bx = 31;
            static constexpr T ax = 1;
            static constexpr bool has_alpha = true;
        };

        struct alignas(2) [[gnu::packed]] bgra_5550
        {
            using T = unsigned;
            T b : 5;
            T g : 5;
            T r : 5;
            T a : 1;

            bgra_5550() noexcept = default;
            constexpr bgra_5550(T cr, T cg, T cb, T) noexcept : b(cb), g(cg), r(cr), a(0) { }

            static constexpr T rx = 31;
            static constexpr T gx = 31;
            static constexpr T bx = 31;
            static constexpr T ax = 0;
            static constexpr bool has_alpha = false;
        };

        struct px { };

        template<typename P>
        struct alignas(P) [[gnu::packed]] pixel : public P, public px
        {
            using T = typename P::T;

            template<typename T>
            union vector
            {
                using V [[gnu::vector_size(4 * sizeof(T))]] = T;
                V v;
                struct { T b, g, r, a; };
                constexpr vector(auto cr, auto cg, auto cb, auto ca) noexcept : b(cb), g(cg), r(cr), a(ca) { }
            };

            constexpr pixel() noexcept = default;
            constexpr pixel(T cr, T cg, T cb, T ca) noexcept : P(cr, cg, cb, ca) { }
            constexpr pixel(T cr, T cg, T cb) noexcept : P(cr, cg, cb, P::ax) { }
            constexpr pixel(const pixel& p) noexcept = default;
            constexpr pixel(pixel&& p) noexcept = default;

            template<typename V>
            constexpr pixel& assign_round(const auto& v) noexcept
            { 
                if constexpr (std::is_integral<typename V::T>::value)
                {
                    this->b = jw::round(v.b);
                    this->g = jw::round(v.g);
                    this->r = jw::round(v.r);
                    if constexpr (V::has_alpha) this->a = jw::round(v.a);
                    return *this;
                }
                this->b = v.b;
                this->g = v.g;
                this->r = v.r;
                if constexpr (V::has_alpha) this->a = v.a;
                return *this;
            };

            template <typename V, typename U, std::enable_if_t<V::has_alpha && U::has_alpha, bool> = { }>
            constexpr pixel<U> cast() const noexcept
            {
                using max_T = decltype(max<U>(U::ax));
                if constexpr (std::is_floating_point<max_T>::value)
                {
                    using vec = vector<max_T>;
                    vec maxp { max<U>(V::rx), max<U>(V::gx), max<U>(V::bx), max<U>(V::ax) };
                    vec maxu { max<U>(U::rx), max<U>(U::gx), max<U>(U::bx), max<U>(U::ax) };
                    vec src { this->r, this->g, this->b, this->a };
                    src.v *= maxu.v;
                    src.v /= maxp.v;
                    return pixel<U> { }.template assign_round<U>(src);
                }
                else 
                {
                    using UT = typename U::T;
                    pixel<U> result { };
                    result.b = static_cast<UT>(this->b * max<U>(U::bx) / max<U>(V::bx));
                    result.g = static_cast<UT>(this->g * max<U>(U::gx) / max<U>(V::gx));
                    result.r = static_cast<UT>(this->r * max<U>(U::rx) / max<U>(V::rx));
                    result.a = static_cast<UT>(this->a * max<U>(U::ax) / max<U>(V::ax));
                    return result;
                }
            };

            template <typename V, typename U, std::enable_if_t<!V::has_alpha && U::has_alpha, bool> = { }>
            constexpr pixel<U> cast() const noexcept
            { 
                using max_T = decltype(max<U>(U::ax));
                if constexpr (std::is_floating_point<max_T>::value)
                {
                    using vec = vector<max_T>;
                    vec maxp { max<U>(V::rx), max<U>(V::gx), max<U>(V::bx), 1 };
                    vec maxu { max<U>(U::rx), max<U>(U::gx), max<U>(U::bx), max<U>(V::ax) };
                    vec src { this->r, this->g, this->b, 1 };
                    src.v *= maxu.v;
                    src.v /= maxp.v;
                    return pixel<U> { }.template assign_round<U>(src);
                }
                else
                {
                    using UT = typename U::T;
                    pixel<U> result { };
                    result.b = static_cast<UT>(this->b * max<U>(U::bx) / max<U>(V::bx));
                    result.g = static_cast<UT>(this->g * max<U>(U::gx) / max<U>(V::gx));
                    result.r = static_cast<UT>(this->r * max<U>(U::rx) / max<U>(V::rx));
                    result.a = U::ax;
                    return result;
                }
            };

            template <typename V, typename U, std::enable_if_t<!U::has_alpha, bool> = { }>
            constexpr pixel<U> cast() const noexcept
            { 
                using max_T = decltype(max<U>(U::ax));
                if constexpr (std::is_floating_point<max_T>::value)
                {
                    using vec = vector<max_T>;
                    vec maxp { max<U>(V::rx), max<U>(V::gx), max<U>(V::bx), 1 };
                    vec maxu { max<U>(U::rx), max<U>(U::gx), max<U>(U::bx), 0 };
                    vec src { this->r, this->g, this->b, 0 };
                    src.v *= maxu.v;
                    src.v /= maxp.v;
                    return pixel<U> { }.template assign_round<U>(src);
                }
                else
                {
                    using UT = typename U::T;
                    pixel<U> result { };
                    result.b = static_cast<UT>(this->b * max<U>(U::bx) / max<U>(V::bx));
                    result.g = static_cast<UT>(this->g * max<U>(U::gx) / max<U>(V::gx));
                    result.r = static_cast<UT>(this->r * max<U>(U::rx) / max<U>(V::rx));
                    return result;
                }
            };

            template <typename U> constexpr pixel<U> cast_to() const noexcept { return cast<P, U>(); }
            template <typename U> constexpr operator pixel<U>() const noexcept { return cast_to<U>(); }

            template <typename U> constexpr pixel& operator=(const pixel<U>& other) noexcept { return blend(other); }
            template <typename U> constexpr pixel& operator=(pixel<U>&& other) noexcept { return *this = std::move(other.template cast_to<P>()); }
            constexpr pixel& operator=(const pixel& other) noexcept { return blend(other); }
            constexpr pixel& operator=(pixel&& o) noexcept = default;

            template <typename U, std::enable_if_t<(std::is_integral<typename U::T>::value && std::is_integral<typename P::T>::value), bool> = { }> 
            static constexpr std::int16_t max(auto max) noexcept { return max + 1; }
            template <typename U, std::enable_if_t<(std::is_floating_point<typename U::T>::value || std::is_floating_point<typename P::T>::value), bool> = { }> 
            static constexpr float max(auto max) noexcept { return max; }

            template<typename V, typename U, std::enable_if_t<!U::has_alpha, bool> = { }>
            constexpr pixel& blend(const pixel<U>& src)
            {
                auto copy = src.template cast_to<V>();
                return assign_round<V>(copy);
            }

            template<typename V, typename U, std::enable_if_t<!V::has_alpha && U::has_alpha, bool> = { }>
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
                        return assign_round<V>(dest);
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
                        return assign_round<V>(dest);
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

                    if constexpr (not std::is_same<V, U>::value) src.v *= maxp.v;
                    src.v *= srca.v;
                    if constexpr (not std::is_same<V, U>::value) src.v /= maxu.v;
                    ax.v -= srca.v;
                    dest.v *= ax.v;
                    dest.v += src.v;
                    dest.v /= maxa.v;
                    return assign_round<V>(dest);
                }
            }

            template<typename V, typename U, std::enable_if_t<V::has_alpha && U::has_alpha, bool> = { }>
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
                        return assign_round<V>(dest);
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
                        return assign_round<V>(dest);
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

                    if (!std::is_same<V, U>::value) src.v *= maxp.v;
                    src.v *= srca.v;
                    if (!std::is_same<V, U>::value) src.v /= maxu.v;
                    ax.v -= srca.v;
                    dest.v *= ax.v;
                    dest.v += src.v;
                    dest.v /= maxa.v;
                    return assign_round<V>(dest);
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

        using pxf = pixel<bgra_ffff>;
        using px32a = pixel<bgra_8888>;
        using px32n = pixel<bgra_8880>;
        using px24 = pixel<bgr_8880>;
        using px16 = pixel<bgr_5650>;
        using px16a = pixel<bgra_5551>;
        using px16n = pixel<bgra_5551>;
        using pxvga = pixel<bgra_6668>;

        static_assert(sizeof(pxf  ) == 16, "check sizeof pixel");
        static_assert(sizeof(px32a) ==  4, "check sizeof pixel");
        static_assert(sizeof(px32n) ==  4, "check sizeof pixel");
        static_assert(sizeof(px24 ) ==  3, "check sizeof pixel");
        static_assert(sizeof(px16 ) ==  2, "check sizeof pixel");
        static_assert(sizeof(px16a) ==  2, "check sizeof pixel");
        static_assert(sizeof(px16n) ==  2, "check sizeof pixel");
        static_assert(sizeof(pxvga) ==  4, "check sizeof pixel");
    }
}
