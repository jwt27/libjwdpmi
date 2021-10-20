/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <string_view>
#include <type_traits>
#include <concepts>
#include <cmath>
#include <jw/common.h>

namespace jw
{
    template<typename T, typename U> constexpr inline auto remainder(T a, U b) { return a % b; }
    template<typename U> constexpr inline auto remainder(long double a, U b) { return __builtin_remainderl(a, b); }
    template<typename U> constexpr inline auto remainder(double a, U b) { return __builtin_remainder(a, b); }
    template<typename U> constexpr inline auto remainder(float a, U b) { return __builtin_remainderf(a, b); }

    template<typename T, typename U> constexpr auto copysign(T a, U b) { return (a > 0) != (b > 0) ? -a : a; }
    template<typename U> constexpr inline auto copysign(long double a, U b) { return __builtin_copysignl(a, b); }
    template<typename U> constexpr inline auto copysign(double a, U b) { return __builtin_copysign(a, b); }
    template<typename U> constexpr inline auto copysign(float a, U b) { return __builtin_copysignf(a, b); }

    template<std::integral T> constexpr inline auto round(T a) { return a; }
    constexpr inline auto round(long double a) { return __builtin_roundl(a); }
    constexpr inline auto round(double a) { return __builtin_round(a); }
    constexpr inline auto round(float a) { return __builtin_roundf(a); }

    constexpr inline auto log2(long double a) { return __builtin_log2l(a); }
    constexpr inline auto log2(double a) { return __builtin_log2(a); }
    constexpr inline auto log2(float a) { return __builtin_log2f(a); }

    template<std::integral T> constexpr inline T shr(T v, int c) noexcept { return (c < 0) ? v << -c : v >> c; }
    template<std::integral T> constexpr inline T shl(T v, int c) noexcept { return (c < 0) ? v >> -c : v << c; }

    template<typename T> inline auto checksum8(const T& value)
    {
        std::uint8_t r { 0 };
        auto* ptr = reinterpret_cast<const byte*>(&value);
        for (std::size_t i = 0; i < sizeof(T); ++i) r += ptr[i];
        return r;
    }

    template<typename CharT, typename Traits> requires (sizeof(CharT) == 1)
    inline auto checksum8(const std::basic_string_view<CharT, Traits>& value)
    {
        std::uint8_t r { 0 };
        for (std::uint8_t c : value) r += c;
        return r;
    }

    template<typename CharT, typename Traits, typename Alloc> requires (sizeof(CharT) == 1)
    inline auto checksum8(const std::basic_string<CharT, Traits, Alloc>& str)
    {
        return checksum8(static_cast<std::basic_string_view<CharT, Traits>>(str));
    }
}
