/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cmath>

namespace jw
{
    template<typename T, typename U> constexpr inline auto remainder(T a, U b) { return a % b; }
    template<typename U> constexpr inline auto remainder(long double a, U b) { return __builtin_remainderl(a, b); }
    template<typename U> constexpr inline auto remainder(double a, U b) { return __builtin_remainder(a, b); }
    template<typename U> constexpr inline auto remainder(float a, U b) { return __builtin_remainderf(a, b); }

    //template<typename T, typename U> constexpr auto copysign(T a, U b) { return std::signbit(a) != std::signbit(b)? -a : a; }
    template<typename T, typename U> constexpr auto copysign(T a, U b) { return (a > 0) != (b > 0) ? -a : a; }
    template<typename U> constexpr inline auto copysign(long double a, U b) { return __builtin_copysignl(a, b); }
    template<typename U> constexpr inline auto copysign(double a, U b) { return __builtin_copysign(a, b); }
    template<typename U> constexpr inline auto copysign(float a, U b) { return __builtin_copysignf(a, b); }

    template<typename T> constexpr inline auto round(T a) { if (std::is_integral<T>::value) return a; }
    template<> constexpr inline auto round(long double a) { return __builtin_roundl(a); }
    template<> constexpr inline auto round(double a) { return __builtin_round(a); }
    template<> constexpr inline auto round(float a) { return __builtin_roundf(a); }

    template<typename T> inline auto checksum8(const T& value)
    {
        std::uint8_t r { 0 };
        auto* ptr = reinterpret_cast<const byte*>(&value);
        for (std::size_t i = 0; i < sizeof(T); ++i) r += ptr[i];
        return r;
    }

    template<> inline auto checksum8(const std::string& value)
    {
        std::uint8_t r { 0 };
        for (auto c : value) r += c;
        return r;
    }
}
