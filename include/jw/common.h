/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstdint>
#include <stdexcept>

using byte = std::uint8_t;
constexpr std::uint64_t operator""  _B(std::uint64_t n) { return n << 00; }
constexpr std::uint64_t operator"" _KB(std::uint64_t n) { return n << 10; }
constexpr std::uint64_t operator"" _MB(std::uint64_t n) { return n << 20; }
constexpr std::uint64_t operator"" _GB(std::uint64_t n) { return n << 30; }
constexpr std::uint64_t operator"" _TB(std::uint64_t n) { return n << 40; }

#define FORCE_FRAME_POINTER asm(""::"r"(__builtin_frame_address(0)));

#ifdef __MMX__
#   define HAVE__MMX__
#endif
#ifdef __SSE__
#   define HAVE__SSE__
#endif

namespace jw
{
    void print_exception(const std::exception& e, int level = 0) noexcept;

    struct terminate_exception
    {
        virtual const char* what() const noexcept { return "Terminating."; }
    };

    [[noreturn]] void terminate();

#   ifdef HAVE__MMX__
    inline constexpr bool mmx = true;
#   else
    inline constexpr bool mmx = false;
#   endif
#   ifdef HAVE__SSE__
    inline constexpr bool sse = true;
#   else
    inline constexpr bool sse = false;
#   endif

    struct empty { };
}
