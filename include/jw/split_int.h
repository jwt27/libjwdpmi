/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstdint>
#pragma GCC diagnostic error "-Wpadded"

namespace jw
{
    namespace detail
    {
        template<typename T, std::size_t size, typename condition = bool>
        union split_int;

        template<typename T, std::size_t size>
        union [[gnu::packed]] split_int<T, size, std::enable_if_t<(size > 16) and (size / 2) % 2 == 0, bool>>
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
        union [[gnu::packed]] split_int<T, size, std::enable_if_t<((size <= 16) or (size / 2) % 2 != 0) and (size % 2) == 0, bool>>
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

    template<typename T, std::size_t N> using split_int [[gnu::aligned(N / 8 < 4 ? N / 8 : 4)]] = detail::split_int<T, N>;

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
