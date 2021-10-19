/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstdint>
#include <type_traits>
#include <limits>
#include <concepts>
#include <jw/math.h>

namespace jw::detail
{
    template<std::size_t> struct larger_int { using type = void; };
    template<> struct larger_int<1> { using type = std::int16_t; };
    template<> struct larger_int<2> { using type = std::int32_t; };
    template<> struct larger_int<4> { using type = std::int64_t; };
    template<> struct larger_int<8> { using type = std::int64_t; };

    template<std::size_t> struct larger_uint { using type = void; };
    template<> struct larger_uint<1> { using type = std::uint16_t; };
    template<> struct larger_uint<2> { using type = std::uint32_t; };
    template<> struct larger_uint<4> { using type = std::uint64_t; };
    template<> struct larger_uint<8> { using type = std::uint64_t; };
}

namespace jw
{
    template<std::integral T, std::integral U>
    using max_t = std::conditional_t<(sizeof(T) >= sizeof(U)), T, U>;

    template<std::integral T>
    using larger_t = std::conditional_t<std::is_signed_v<T>, typename detail::larger_int<sizeof(T)>::type,
                                                             typename detail::larger_uint<sizeof(T)>::type>;

    template<typename T, typename U>
    concept same_sign_int = std::is_signed_v<T> == std::is_signed_v<U>
                            and std::integral<T> and std::integral<U>;

    template<typename T, std::size_t F> requires std::integral<T>
    struct fixed
    {
        static constexpr std::size_t bits = std::numeric_limits<T>::digits;
        static constexpr std::size_t int_bits = bits - F;
        static constexpr std::size_t frac_bits = F;
        static_assert(frac_bits <= bits);

        T value;

        static fixed make(T value) noexcept { return fixed { noshift, value }; }

        template<std::floating_point U>
        constexpr fixed(U v) noexcept : value { static_cast<T>(round(v * (1 << F))) } { }

        template<std::integral U>
        constexpr fixed(U v) noexcept : value { static_cast<T>(v) << F } { }

        template<std::integral U, std::size_t G>
        constexpr fixed(const fixed<U, G>& v) noexcept : value { shr(static_cast<T>(v.value), F - G) } { }

        constexpr fixed() noexcept = default;
        constexpr fixed(const fixed&) noexcept = default;
        constexpr fixed(fixed&&) noexcept = default;
        constexpr fixed& operator=(const fixed&) noexcept = default;
        constexpr fixed& operator=(fixed&&) noexcept = default;

        template<typename U> constexpr fixed& operator =(U v) { *this  = fixed { v }; return *this; }

        constexpr fixed& operator+=(const fixed& v) { value += v.value; return *this; }
        constexpr fixed& operator-=(const fixed& v) { value -= v.value; return *this; }
        constexpr fixed& operator*=(const fixed& v) { value *= v.value; value >>= F; return *this; }
        constexpr fixed& operator/=(const fixed& v) { value <<= F; value /= v.value; return *this; }

        template<same_sign_int<T> U, std::size_t G> friend constexpr auto operator+(const fixed& f, const fixed<U, G>& v)
        {
            fixed<max_t<T, U>, std::max(F, G)> a { f }, b { v };
            return a += b;
        }
        template<same_sign_int<T> U, std::size_t G> friend constexpr auto operator-(const fixed& f, const fixed<U, G>& v)
        {
            fixed<max_t<T, U>, std::max(F, G)> a { f }, b { v };
            return a -= b;
        }
        template<same_sign_int<T> U, std::size_t G> friend constexpr auto operator*(const fixed& f, const fixed<U, G>& v)
        {
            larger_t<max_t<T, U>> a { f.value };
            return fixed<larger_t<max_t<T, U>>, F + G> { noshift, a * v.value };
        }
        template<same_sign_int<T> U, std::size_t G> friend constexpr auto operator/(const fixed& f, const fixed<U, G>& v)
        {
            if constexpr (static_cast<signed>(F - G) <= 0)
                return (static_cast<larger_t<T>>(f.value) << -(F - G)) / v.value;
            else return fixed<max_t<T, U>, F - G> { noshift, f.value /= v.value };
        }

        template<std::integral U> constexpr fixed& operator+=(U v) { value += static_cast<larger_t<U>>(v) << F; return *this; }
        template<std::integral U> constexpr fixed& operator-=(U v) { value -= static_cast<larger_t<U>>(v) << F; return *this; }
        template<std::integral U> constexpr fixed& operator*=(U v) { value *= v; return *this; }
        template<std::integral U> constexpr fixed& operator/=(U v) { value /= v; return *this; }

        template<std::floating_point U> constexpr fixed& operator+=(U v) { value = round(value + v * (1 << F)); return *this; }
        template<std::floating_point U> constexpr fixed& operator-=(U v) { value = round(value - v * (1 << F)); return *this; }
        template<std::floating_point U> constexpr fixed& operator*=(U v) { value = round(value * v); return *this; }
        template<std::floating_point U> constexpr fixed& operator/=(U v) { value = round(value / v); return *this; }

        template<std::integral U> friend constexpr auto operator+(const fixed& f, U v) { return fixed<max_t<T, U>, F> { f } += v; }
        template<std::integral U> friend constexpr auto operator-(const fixed& f, U v) { return fixed<T, F> { f } -= v; }
        template<std::integral U> friend constexpr auto operator*(const fixed& f, U v) { return fixed<larger_t<T>, F> { f } *= v; }
        template<std::integral U> friend constexpr auto operator/(const fixed& f, U v) { return fixed<T, F> { f } /= v; }

        template<std::integral U> friend constexpr auto operator+(U v, const fixed& f) { return f + v; }
        template<std::integral U> friend constexpr auto operator-(U v, const fixed& f) { return f - v; }
        template<std::integral U> friend constexpr auto operator*(U v, const fixed& f) { return f * v; }
        template<std::integral U> friend constexpr auto operator/(U v, const fixed& f) { return (static_cast<larger_t<U>>(v) << F) / f.value; }

        template<std::floating_point U> friend constexpr auto operator+(const fixed& f, U v) { return static_cast<U>(f) + v; }
        template<std::floating_point U> friend constexpr auto operator-(const fixed& f, U v) { return static_cast<U>(f) - v; }
        template<std::floating_point U> friend constexpr auto operator*(const fixed& f, U v) { return static_cast<U>(f) * v; }
        template<std::floating_point U> friend constexpr auto operator/(const fixed& f, U v) { return static_cast<U>(f) / v; }

        constexpr fixed& operator>>=(unsigned v) { value >>= v; return *this; }
        constexpr fixed& operator<<=(unsigned v) { value <<= v; return *this; }

        friend constexpr fixed operator>>(const fixed& f, unsigned v) { return fixed { f } >>= v; }
        friend constexpr fixed operator<<(const fixed& f, unsigned v) { return fixed { f } <<= v; }

        template<std::floating_point U> constexpr operator U() const noexcept { return static_cast<U>(value) / (1 << F); }
        template<std::integral U> constexpr explicit operator U() const noexcept { return static_cast<U>(value) >> F; }

    private:
        struct { } constexpr static noshift { };
        template<std::integral U> constexpr fixed(decltype(noshift), U v) noexcept : value { static_cast<T>(v) } { }
    };

    template<typename T, std::size_t F>
    constexpr T round(const fixed<T, F>& f) noexcept { return (f.value + (1 << (F - 1))) >> F; }
}
