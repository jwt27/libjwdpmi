/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <variant>

namespace jw
{
    template <typename V, typename T, std::size_t I = 0>
    consteval bool variant_contains()
    {
        if constexpr (I >= std::variant_size_v<V>) return false;
        else if constexpr (std::is_same_v<T, std::variant_alternative_t<I, V>>) return true;
        else return variant_contains<V, T, I + 1>();
    }

    template <typename V, typename T, std::size_t I = 0>
    consteval std::size_t variant_index()
    {
        static_assert(variant_contains<V, T>());
        if constexpr (not variant_contains<V, T>()) return std::variant_npos;
        else if constexpr (std::is_same_v<T, std::variant_alternative_t<I, V>>) return I;
        else return variant_index<V, T, I + 1>();
    }

    template<std::size_t I = 0, typename F, typename V>
    constexpr decltype(auto) visit(F&& visitor, V&& variant)
    {
        constexpr auto size = std::variant_size_v<std::remove_cvref_t<V>>;
        if (auto* p = std::get_if<I>(&std::forward<V>(variant)))
            return std::forward<F>(visitor)(*p);
        else if constexpr (I + 1 < size)
            return visit<I + 1>(std::forward<F>(visitor), std::forward<V>(variant));
        throw std::bad_variant_access { };
    }
}
