/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstdint>
#include <type_traits>
#include <algorithm>
#include <new>

namespace jw
{
    template<typename T> struct remove_address_space { using type = T; };
    template<typename T> struct remove_address_space<__seg_fs T> { using type = T; };
    template<typename T> struct remove_address_space<__seg_gs T> { using type = T; };
    template<typename T> using remove_address_space_t = remove_address_space<T>::type;

    template<typename To, typename From> struct copy_address_space { using type = To; };
    template<typename To, typename From> struct copy_address_space<To, __seg_fs From> { using type = __seg_fs To; };
    template<typename To, typename From> struct copy_address_space<To, __seg_gs From> { using type = __seg_gs To; };
    template<typename To, typename From> using copy_address_space_t = copy_address_space<To, From>::type;

    template<typename T, typename U> struct same_address_space : std::true_type { };
    template<typename T, typename U> struct same_address_space<T, __seg_fs U> : std::false_type { };
    template<typename T, typename U> struct same_address_space<T, __seg_gs U> : std::false_type { };
    template<typename T, typename U> struct same_address_space<__seg_fs T, U> : std::false_type { };
    template<typename T, typename U> struct same_address_space<__seg_gs T, U> : std::false_type { };
    template<typename T, typename U> struct same_address_space<__seg_fs T, __seg_gs U> : std::false_type { };
    template<typename T, typename U> struct same_address_space<__seg_gs T, __seg_fs U> : std::false_type { };
    template<typename T, typename U> inline constexpr bool same_address_space_v = same_address_space<T, U>::value;

    template<typename T> struct default_address_space : std::true_type { };
    template<typename T> struct default_address_space<__seg_fs T> : std::false_type { };
    template<typename T> struct default_address_space<__seg_gs T> : std::false_type { };
    template<typename T, typename U> inline constexpr bool default_address_space_v = default_address_space<T>::value;

    template<typename T> struct fs_address_space : std::false_type { };
    template<typename T> struct fs_address_space<__seg_fs T> : std::true_type { };
    template<typename T> struct fs_address_space<__seg_gs T> : std::false_type { };
    template<typename T, typename U> inline constexpr bool fs_address_space_v = fs_address_space<T>::value;

    template<typename T> struct gs_address_space : std::false_type { };
    template<typename T> struct gs_address_space<__seg_fs T> : std::false_type { };
    template<typename T> struct gs_address_space<__seg_gs T> : std::true_type { };
    template<typename T, typename U> inline constexpr bool gs_address_space_v = gs_address_space<T>::value;

    template<typename T, typename U>
    concept any_address_space = std::is_base_of_v<U, remove_address_space_t<T>>;

    template<typename T, typename U>
    inline T* far_copy(T* dst, const U* src, std::size_t num = 1)
    {
        using dst_type = remove_address_space_t<T>;
        using src_type = remove_address_space_t<U>;
        static_assert(std::is_same_v<dst_type, src_type> or
                      std::is_base_of_v<src_type, dst_type> or
                      std::is_base_of_v<dst_type, src_type>);
        static_assert(std::is_trivially_copy_assignable_v<dst_type>);
        const std::size_t size = std::min(sizeof(dst_type), sizeof(src_type));
        for (std::size_t i = 0; i < num; ++i)
        {
            {
                const std::size_t n = size / sizeof(std::uint32_t);
                auto* const d = reinterpret_cast<copy_address_space_t<std::uint32_t, T>*>(dst + i);
                auto* const s = reinterpret_cast<copy_address_space_t<const std::uint32_t, U>*>(src + i);
                for (std::size_t j = 0; j < n; ++j) d[j] = s[j];
            }
            {
                const std::size_t n = size % sizeof(std::uint32_t);
                auto* const d = reinterpret_cast<copy_address_space_t<std::byte, T>*>(dst + i) + (size - n);
                auto* const s = reinterpret_cast<copy_address_space_t<const std::byte, U>*>(src + i) + (size - n);
                for (std::size_t j = 0; j < n; ++j) d[j] = s[j];
            }
        }
        return std::launder(dst);
    }
}
