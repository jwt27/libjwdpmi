/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstdint>
#include <algorithm>
#include <bit>
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wpadded"
#pragma GCC diagnostic ignored "-Wpacked-not-aligned"

namespace jw
{
    namespace detail
    {
        consteval inline std::size_t alignment_for_bits(std::size_t nbits, std::size_t max) noexcept
        {
            return std::min(static_cast<std::size_t>(std::bit_ceil((nbits - 1) / 8 + 1)), max);
        }

        template<typename, std::size_t, typename = bool>
        union split_int;

        template<typename T, std::size_t size>
        union [[gnu::packed]] alignas(alignment_for_bits(size, 4))
            split_int<T, size, std::enable_if_t<(size > 16) and (size / 2) % 2 == 0, bool>>
        {
            struct [[gnu::packed]]
            {
                split_int<unsigned, (size / 2)> lo;
                split_int<T, (size / 2)> hi;
            };
            std::conditional_t<std::is_signed_v<T>, std::int64_t, std::uint64_t> value : size;

            constexpr split_int() noexcept = default;
            constexpr split_int(const split_int&) noexcept = default;
            constexpr split_int(split_int&&) noexcept = default;
            constexpr split_int& operator=(const split_int&) noexcept = default;
            constexpr split_int& operator=(split_int&&) noexcept = default;

            template<typename L, typename H>
            constexpr split_int(L&& l, H&& h) noexcept : lo { std::forward<L>(l) }, hi { std::forward<H>(h) } { };
            constexpr split_int(const auto& v) noexcept : value { static_cast<decltype(value)>(v) } { };
            constexpr operator auto() const noexcept { return value; }
        };

        template<typename T, std::size_t size>
        union [[gnu::packed]] alignas(alignment_for_bits(size, 4))
            split_int<T, size, std::enable_if_t<((size <= 16) or (size / 2) % 2 != 0) and (size % 2) == 0, bool>>
        {
            struct [[gnu::packed]]
            {
                 unsigned lo : size / 2;
                 T hi : size / 2;
            };
            std::conditional_t<std::is_signed_v<T>, std::int64_t, std::uint64_t> value : size;

            constexpr split_int() noexcept = default;
            constexpr split_int(const split_int&) noexcept = default;
            constexpr split_int(split_int&&) noexcept = default;
            constexpr split_int& operator=(const split_int&) noexcept = default;
            constexpr split_int& operator=(split_int&&) noexcept = default;

            constexpr split_int(const auto& l, const auto& h) noexcept : lo { static_cast<unsigned>(l) }, hi { static_cast<T>(h) } { };
            constexpr split_int(const auto& v) noexcept : value { static_cast<decltype(value)>(v) } { };
            constexpr operator auto() const noexcept { return value; }
        };
    }

    template<typename T, std::size_t N>
    using split_int = detail::split_int<T, N>;

    using split_uint16_t = split_int<unsigned, 16>;
    using split_uint32_t = split_int<unsigned, 32>;
    using split_uint64_t = split_int<unsigned, 64>;
    using split_int16_t = split_int<signed, 16>;
    using split_int32_t = split_int<signed, 32>;
    using split_int64_t = split_int<signed, 64>;

    static_assert(sizeof(split_uint64_t) == 8);
    static_assert(sizeof(split_uint32_t) == 4);
    static_assert(sizeof(split_uint16_t) == 2);
    static_assert(alignof(split_uint64_t) == 4);
    static_assert(alignof(split_uint32_t) == 4);
    static_assert(alignof(split_uint16_t) == 2);
}

#pragma GCC diagnostic pop
