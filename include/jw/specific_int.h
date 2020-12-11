/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstdint>
#include <jw/common.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wpadded"

namespace jw
{
    namespace detail
    {
        template<typename T, std::size_t N>
        struct [[gnu::packed]] alignas(alignment_for_bits(N, 4)) specific_int
        {
            static_assert(N <= sizeof(std::intmax_t) * 8);
            std::conditional_t<std::is_signed_v<T>, std::intmax_t, std::uintmax_t> value : N;

            constexpr specific_int() noexcept = default;
            constexpr specific_int(const specific_int&) noexcept = default;
            constexpr specific_int(specific_int&&) noexcept = default;
            constexpr specific_int& operator=(const specific_int&) noexcept = default;
            constexpr specific_int& operator=(specific_int&&) noexcept = default;

            constexpr specific_int(std::integral auto v) noexcept : value { static_cast<decltype(value)>(v) } { };
            constexpr operator auto() const noexcept { return value; }
        };
    }

    template<std::size_t N> using specific_int = detail::specific_int<signed, N>;
    template<std::size_t N> using specific_uint = detail::specific_int<unsigned, N>;

    static_assert( sizeof(specific_uint<48>) == 6);
    static_assert( sizeof(specific_uint<24>) == 3);
    static_assert( sizeof(specific_uint<12>) == 2);
    static_assert( sizeof(specific_uint< 6>) == 1);
    static_assert(alignof(specific_uint<48>) == 2);
    static_assert(alignof(specific_uint<24>) == 1);
    static_assert(alignof(specific_uint<12>) == 1);
    static_assert(alignof(specific_uint< 6>) == 1);
}

#pragma GCC diagnostic pop
