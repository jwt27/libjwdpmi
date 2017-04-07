/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/common.h>

namespace jw
{
    namespace video
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
                constexpr char_with_attr(char c) noexcept : character(c), attr() { }
            } value;
            std::uint16_t raw_value;

            constexpr text_char() noexcept : text_char(' ') { }
            constexpr text_char(char c, byte fcol = 7, byte bcol = 0, bool _blink = false) noexcept : value(c, fcol, bcol, _blink) { }
            constexpr explicit text_char(std::uint16_t v) noexcept : raw_value(v) { }
            constexpr explicit operator std::uint16_t() const noexcept{ return raw_value; }
            constexpr text_char& operator=(char c) noexcept { value.character = c; return *this; }
            constexpr operator char() const noexcept{ return value.character; }
        };
        static_assert(sizeof(text_char) == 2 && alignof(text_char) == 2, "text_char has incorrect size or alignment.");

        struct alignas(0x10) bgra_ffff
        {
            using T = float;
            using V [[gnu::vector_size(4 * sizeof(T)), gnu::may_alias]] = T;
            union
            {
                V vector;
                struct { T b, g, r, a; };
            };

            bgra_ffff() noexcept = default;
            constexpr bgra_ffff(T cr, T cg, T cb, T ca) noexcept : b(cb), g(cg), r(cr), a(ca) { }

            static constexpr T rx = 1.0f;
            static constexpr T gx = 1.0f;
            static constexpr T bx = 1.0f;
            static constexpr T ax = 1.0f;
            static constexpr bool has_alpha = true;
            static constexpr bool has_vector = true;
        };

        struct alignas(4) bgra_8888
        {
            using T = std::uint8_t;
            using V [[gnu::vector_size(4 * sizeof(T)), gnu::may_alias]] = T;
            union
            {
                V vector;
                struct { T b, g, r, a; };
            };

            bgra_8888() noexcept = default;
            constexpr bgra_8888(T cr, T cg, T cb, T ca) noexcept : b(cb), g(cg), r(cr), a(ca) { }

            static constexpr T rx = 255;
            static constexpr T gx = 255;
            static constexpr T bx = 255;
            static constexpr T ax = 255;
            static constexpr bool has_alpha = true;
            static constexpr bool has_vector = true;
        };

        struct [[gnu::packed]] bgra_8880
        {
            using T = std::uint8_t;
            T b, g, r;

            bgra_8880() noexcept = default;
            constexpr bgra_8880(T cr, T cg, T cb, T) noexcept : b(cb), g(cg), r(cr) { }

            static constexpr T rx = 255;
            static constexpr T gx = 255;
            static constexpr T bx = 255;
            static constexpr T ax = 0;
            static constexpr bool has_alpha = false;
            static constexpr bool has_vector = false;
        };

        struct bgra_6668
        {
            using T = unsigned;
            using V [[gnu::vector_size(4), gnu::may_alias]] = std::uint8_t;
            union
            {
                V vector;
                struct [[gnu::packed]]
                {
                    T b : 6, : 2;
                    T g : 6, : 2;
                    T r : 6, : 2;
                    T a : 8;
                };
            };

            bgra_6668() noexcept = default;
            constexpr bgra_6668(T cr, T cg, T cb, T ca) noexcept : b(cb), g(cg), r(cr), a(ca) { }

            static constexpr T rx = 63;
            static constexpr T gx = 63;
            static constexpr T bx = 63;
            static constexpr T ax = 255;
            static constexpr bool has_alpha = true;
            static constexpr bool has_vector = true;
        };

        struct alignas(2) [[gnu::packed]] bgra_5650
        {
            using T = unsigned;
            T b : 5;
            T g : 6;
            T r : 5;

            bgra_5650() noexcept = default;
            constexpr bgra_5650(T cr, T cg, T cb, T) noexcept : b(cb), g(cg), r(cr) { }

            static constexpr T rx = 31;
            static constexpr T gx = 63;
            static constexpr T bx = 31;
            static constexpr T ax = 0;
            static constexpr bool has_alpha = false;
            static constexpr bool has_vector = false;
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
            static constexpr bool has_vector = false;
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
                constexpr vector(T cr, T cg, T cb, T ca) noexcept : b(cb), g(cg), r(cr), a(ca) { }
            };

            constexpr pixel() noexcept = default;
            constexpr pixel(T cr, T cg, T cb, T ca) noexcept : P(cr, cg, cb, ca) { }
            constexpr pixel(T cr, T cg, T cb) noexcept : P(cr, cg, cb, P::ax) { }
            constexpr pixel(const pixel& p) noexcept = default;
            constexpr pixel(pixel&& p) noexcept = default;

            template <typename U> constexpr pixel<U> cast(std::true_type) const noexcept
            { 
                return pixel<U> 
                { 
                    static_cast<typename U::T>(this->r * max<U>(U::rx) / max<U>(P::rx)),
                    static_cast<typename U::T>(this->g * max<U>(U::gx) / max<U>(P::gx)),
                    static_cast<typename U::T>(this->b * max<U>(U::bx) / max<U>(P::bx)),
                    static_cast<typename U::T>(this->a * max<U>(U::ax) / max<U>(P::ax))
                };
            };

            template <typename U> constexpr pixel<U> cast(std::false_type) const noexcept
            { 
                return pixel<U> 
                { 
                    static_cast<typename U::T>(this->r * max<U>(U::rx) / max<U>(P::rx)),
                    static_cast<typename U::T>(this->g * max<U>(U::gx) / max<U>(P::gx)),
                    static_cast<typename U::T>(this->b * max<U>(U::bx) / max<U>(P::bx))
                };
            };

            template <typename U> constexpr pixel<U> cast() const noexcept { return cast<U>(std::is_void<std::conditional_t<P::has_alpha, void, bool>> { }); }
            template <typename U> constexpr operator pixel<U>() const noexcept { return cast<U>(); }

            template <typename U> constexpr pixel& operator=(const pixel<U>& other) noexcept { return blend(other); }
            template <typename U> constexpr pixel& operator=(pixel<U>&& other) noexcept { return *this = std::move(other.cast<P>()); }
            constexpr pixel& operator=(const pixel& other) noexcept { return blend(other); }
            constexpr pixel& operator=(pixel&& o) noexcept = default;

            template <typename U, std::enable_if_t<(std::is_integral<typename U::T>::value && std::is_integral<typename P::T>::value), bool> = { }> 
            static constexpr std::int16_t max(auto max) noexcept { return max + 1; }
            template <typename U, std::enable_if_t<(std::is_floating_point<typename U::T>::value || std::is_floating_point<typename P::T>::value), bool> = { }> 
            static constexpr float max(auto max) noexcept { return max; }

            template<typename V, std::enable_if_t<!V::has_alpha, bool> = { }>
            constexpr pixel& blend(const pixel<V>& src)
            {
                auto copy = src.cast<P>();
                this->b = copy.b;
                this->g = copy.g;
                this->r = copy.r;
                return *this;
            }

            template<typename V, std::enable_if_t<!P::has_alpha && V::has_alpha, bool> = { }>
            constexpr pixel& blend(const pixel<V>& src)
            {
                this->b = ((src.b * max<V>(P::bx) * src.a) / max<V>(V::bx) + this->b * (src.ax - src.a)) / max<V>(V::ax);
                this->g = ((src.g * max<V>(P::gx) * src.a) / max<V>(V::gx) + this->g * (src.ax - src.a)) / max<V>(V::ax);
                this->r = ((src.r * max<V>(P::rx) * src.a) / max<V>(V::rx) + this->r * (src.ax - src.a)) / max<V>(V::ax);
                return *this;
            }

            template<typename V, std::enable_if_t<P::has_alpha && V::has_alpha, bool> = { }>
            pixel& blend(const pixel<V>& other)
            {
                using vec = vector<decltype(max<V>(P::ax))>;
                vec maxp { max<V>(P::rx), max<V>(P::gx), max<V>(P::bx), max<V>(P::ax) };
                vec maxv { max<V>(V::rx), max<V>(V::gx), max<V>(V::bx), max<V>(V::ax) };
                vec maxa { max<V>(V::ax), max<V>(V::ax), max<V>(V::ax), max<V>(V::ax) };
                vec ax { V::ax, V::ax, V::ax, V::ax };

                vec src { other.r, other.g, other.b, other.a };
                vec dest { this->r, this->g, this->b, this->a };
                vec srca { src.a, src.a, src.a, src.a };

                src.v *= maxp.v;
                src.v *= srca.v;
                src.v /= maxv.v;
                ax.v -= srca.v;
                dest.v *= ax.v;
                dest.v += src.v;
                dest.v /= maxa.v;
                return *this = pixel { dest.r, dest.b, dest.g, dest.a };
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
        using px32 = pixel<bgra_8888>;
        using px24 = pixel<bgra_8880>;
        using px16 = pixel<bgra_5650>;
        using px15 = pixel<bgra_5551>;
        using pxvga = pixel<bgra_6668>;

        static_assert(sizeof(pxf  ) == 16, "check sizeof pixel");
        static_assert(sizeof(px32 ) ==  4, "check sizeof pixel");
        static_assert(sizeof(px24 ) ==  3, "check sizeof pixel");
        static_assert(sizeof(px16 ) ==  2, "check sizeof pixel");
        static_assert(sizeof(px15 ) ==  2, "check sizeof pixel");
        static_assert(sizeof(pxvga) ==  4, "check sizeof pixel");
    }
}
