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

#include <functional>
#include <algorithm>
#include <type_traits>

namespace jw
{
    template <typename T>
    struct enum_struct
    {
        using underlying_type = T;

        constexpr enum_struct(T v) : value(v) { }
        constexpr enum_struct() : value(0) { }

        constexpr operator T() const { return value; }
        constexpr bool operator==(T v) const { return value == v; }
        constexpr bool operator==(const enum_struct<T>& v) const { return value == v.value; }
        constexpr enum_struct& operator=(T v) { value = v; return *this; }
        constexpr enum_struct& operator=(const enum_struct& v) { value = v.value; return *this; }

        constexpr auto hash_value() const noexcept { return std::hash<T>()(value); }

    //protected:
        T value;
    };
}

#define SPECIALIZE_STD_HASH(T)                                                      \
namespace std                                                                       \
{                                                                                   \
    template<>                                                                      \
    struct hash<T>                                                                  \
    {                                                                               \
        using argument_type = T;                                                    \
        using underlying_type = typename T::underlying_type;                        \
        using result_type = typename std::hash<underlying_type>::result_type;       \
                                                                                    \
        result_type operator()(const argument_type& arg) const noexcept             \
        {                                                                           \
            return std::hash<underlying_type>()(arg.value);                         \
        }                                                                           \
    };                                                                              \
}
