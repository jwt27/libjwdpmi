/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <compare>

namespace jw
{
    struct simd
    {
        enum flags
        {
            none        = 0b0,
            mmx         = 0b1,     // MMX
            mmx2        = 0b10,    // MMX extensions (introduced with SSE)
            amd3dnow    = 0b100,   // 3DNow!
            amd3dnow2   = 0b1000,  // 3DNow! extensions
            sse         = 0b10000  // SSE
        } value;

        constexpr simd() noexcept = default;
        constexpr simd(simd&&) noexcept = default;
        constexpr simd(const simd&) noexcept = default;
        constexpr simd& operator=(simd&&) noexcept = default;
        constexpr simd& operator=(const simd&) noexcept = default;

        constexpr simd(flags f) noexcept : value { f } { }
        constexpr simd& operator=(flags f) noexcept { value = f; return *this; }

        constexpr std::strong_ordering operator<=>(const simd&) const noexcept = default;
        constexpr explicit operator bool() const noexcept { return value != none; }

        constexpr simd& operator|=(simd a) noexcept { return *this = *this | a; }
        constexpr simd  operator| (simd a) const noexcept { return { static_cast<flags>(value | a.value) }; }

        constexpr simd& operator&=(simd a) noexcept { return *this = *this & a; }
        constexpr simd  operator& (simd a) const noexcept { return { static_cast<flags>(value & a.value) }; }

        constexpr bool match(simd target) const noexcept { return (*this & target) == target; }
    };

    constexpr simd operator|(simd::flags a, simd::flags b) noexcept { return simd { a } | simd { b }; }
    constexpr simd operator&(simd::flags a, simd::flags b) noexcept { return simd { a } & simd { b }; }
}
