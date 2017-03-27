/******************************* libjwdpmi **********************************
Copyright (C) 2016-2017  J.W. Jagersma

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once
#include <cstdint>

namespace jw
{
    constexpr auto constexpr_min(auto a, auto b) { return std::min(a, b); }

    template<typename T, std::size_t size>
    union alignas(constexpr_min(size >> 3, 4ul)) [[gnu::packed]] split_int
    {
        struct [[gnu::packed]]
        {
            split_int<unsigned, (size >> 1)> lo;
            split_int<T, (size >> 1)> hi;
        };
        std::conditional_t<std::is_signed<T>::value, std::int64_t, std::uint64_t> value : size;
        constexpr split_int() noexcept { }
        constexpr split_int(auto v) noexcept : value(v) { };
        constexpr operator auto() const noexcept { return value; }
    };

    template<typename T>
    union alignas(1) [[gnu::packed]] split_int<T, 8>
    {
        std::conditional_t<std::is_signed<T>::value, std::int8_t, std::uint8_t> value;
        constexpr split_int() noexcept { }
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

    union [[gnu::packed]] split_int14_t
    {                      
        struct[[gnu::packed]]
        {
            unsigned lo : 7;
            unsigned hi : 7;
        };
        signed value : 14;
        constexpr split_int14_t() noexcept : value(0) { }
        constexpr split_int14_t(auto v) noexcept : value(v) { }
        constexpr operator auto() const noexcept { return value; }
    };
}
