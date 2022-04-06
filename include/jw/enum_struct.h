/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

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
        constexpr enum_struct& operator=(T v) { value = v; return *this; }
        constexpr enum_struct& operator=(const enum_struct& v) { value = v.value; return *this; }

        constexpr auto hash_value() const noexcept { return std::hash<T>()(value); }

    //protected:
        T value;
    };
}

namespace std
{
    template <typename T> requires (std::is_base_of_v<jw::enum_struct<typename T::underlying_type>, T>)
    struct hash<T>
    {
        std::size_t operator()(const T& arg) const noexcept
        {
            return arg.hash_value();
        }
    };
}
