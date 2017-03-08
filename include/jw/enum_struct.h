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
