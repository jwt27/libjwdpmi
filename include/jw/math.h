/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once

namespace jw
{
    template<typename T> constexpr T remainder(T a, T b) { return a % b; }
    template<> constexpr long double remainder(long double a, long double b) { return __builtin_remainderl(a, b); }
    template<> constexpr double remainder(double a, double b) { return __builtin_remainder(a, b); }
    template<> constexpr float remainder(float a, float b) { return __builtin_remainderf(a, b); }
}
