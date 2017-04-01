/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once

namespace jw
{
    template<typename T, typename U> constexpr T remainder(T a, U b) { return a % b; }
    template<> constexpr long double remainder(long double a, long double b) { return __builtin_remainderl(a, b); }
    template<> constexpr double remainder(double a, double b) { return __builtin_remainder(a, b); }
    template<> constexpr float remainder(float a, float b) { return __builtin_remainderf(a, b); }

    template<typename T, typename U> constexpr T copysign(T a, U b) { return std::signbit(a) != std::signbit(b)? -a : a; }
    template<> constexpr long double copysign(long double a, long double b) { return __builtin_copysignl(a, b); }
    template<> constexpr double copysign(double a, double b) { return __builtin_copysign(a, b); }
    template<> constexpr float copysign(float a, float b) { return __builtin_copysignf(a, b); }
}
