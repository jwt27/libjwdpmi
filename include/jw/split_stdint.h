/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstdint>

namespace jw
{
    constexpr auto constexpr_min(auto a, auto b) { return std::min(a, b); }

    template<typename T, std::size_t size, typename condition = bool>
    union alignas(constexpr_min(size >> 3, 4ul))[[gnu::packed]] split_int;

    template<typename T, std::size_t size>
    union alignas(constexpr_min(size >> 3, 4ul)) [[gnu::packed]] split_int<T, size, std::enable_if_t<(size >> 1) % 2 == 0, bool>>
    {
        struct[[gnu::packed]]
        {
            split_int<unsigned, (size >> 1)> lo;
            split_int<T, (size >> 1)> hi;
        };
        std::conditional_t<std::is_signed_v<T>, std::int64_t, std::uint64_t> value : size;
        constexpr split_int() noexcept = default;
        constexpr split_int(auto v) noexcept : value(v) { };
        constexpr operator auto() const noexcept { return value; }
    };

    template<typename T, std::size_t size>
    union alignas(constexpr_min(size >> 3, 4ul)) [[gnu::packed]] split_int<T, size, std::enable_if_t<(size >> 1) % 2 != 0, bool>>
    {
        struct[[gnu::packed]]
        {
             unsigned lo : size >> 1;
             T hi : size >> 1;
        };
        std::conditional_t<std::is_signed_v<T>, std::int64_t, std::uint64_t> value : size;
        constexpr split_int() noexcept = default;
        constexpr split_int(auto v) noexcept : value(v) { };
        constexpr operator auto() const noexcept { return value; }
    };

    template<typename T>
    union alignas(1) [[gnu::packed]] split_int<T, 8, bool>
    {
        std::conditional_t<std::is_signed<T>::value, std::int8_t, std::uint8_t> value;
        constexpr split_int() noexcept = default;
        constexpr split_int(auto v) noexcept : value(v) { }
        constexpr operator auto() const noexcept { return value; }
    };

    using split_uint16_t = split_int<unsigned, 16>;
    using split_uint32_t = split_int<unsigned, 32>;
    using split_uint64_t = split_int<unsigned, 64>;
    using split_int16_t = split_int<signed, 16>;
    using split_int32_t = split_int<signed, 32>;
    using split_int64_t = split_int<signed, 64>;

    static_assert(sizeof(split_uint64_t) == 8, "check sizeof jw::split_int");
    static_assert(alignof(split_uint64_t) == 4, "check alignof jw::split_int");
}
