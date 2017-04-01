/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once

namespace jw
{
    template<typename T, typename U> constexpr inline auto remainder(T a, U b) { return a % b; }
    template<> constexpr inline auto remainder(long double a, long double b) { return __builtin_remainderl(a, b); }
    template<> constexpr inline auto remainder(double a, double b) { return __builtin_remainder(a, b); }
    template<> constexpr inline auto remainder(float a, float b) { return __builtin_remainderf(a, b); }

    template<typename T, typename U> constexpr auto copysign(T a, U b) { return std::signbit(a) != std::signbit(b)? -a : a; }
    template<> constexpr inline auto copysign(long double a, long double b) { return __builtin_copysignl(a, b); }
    template<> constexpr inline auto copysign(double a, double b) { return __builtin_copysign(a, b); }
    template<> constexpr inline auto copysign(float a, float b) { return __builtin_copysignf(a, b); }

    template<typename T> constexpr inline auto round(T a) { if (std::is_integral<T>::value) return a; }
    template<> constexpr inline auto round(long double a) { return __builtin_roundl(a); }
    template<> constexpr inline auto round(double a) { return __builtin_round(a); }
    template<> constexpr inline auto round(float a) { return __builtin_roundf(a); }
}
