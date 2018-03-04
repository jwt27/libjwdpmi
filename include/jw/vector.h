/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstdint>
#include <utility>
#include <cmath>
#include <jw/math.h>

namespace jw
{
    namespace detail
    {
        template<unsigned N, typename T, typename = void> struct vector_size { static constexpr std::size_t size { N * sizeof(T) }; };
#       ifdef __MMX__
        template<unsigned N> struct vector_size<N, std::int32_t,  std::enable_if_t<N <= 2>> { static constexpr std::size_t size { 8 }; };
        template<unsigned N> struct vector_size<N, std::uint32_t, std::enable_if_t<N <= 2>> { static constexpr std::size_t size { 8 }; };
        template<unsigned N> struct vector_size<N, std::int16_t,  std::enable_if_t<N <= 4>> { static constexpr std::size_t size { 8 }; };
        template<unsigned N> struct vector_size<N, std::uint16_t, std::enable_if_t<N <= 4>> { static constexpr std::size_t size { 8 }; };
        template<unsigned N> struct vector_size<N, std::int8_t,   std::enable_if_t<N <= 8>> { static constexpr std::size_t size { 8 }; };
        template<unsigned N> struct vector_size<N, std::uint8_t,  std::enable_if_t<N <= 8>> { static constexpr std::size_t size { 8 }; };
#       endif
#       ifdef __SSE__
        template<unsigned N> struct vector_size<N, float, std::enable_if_t<N <= 4>> { static constexpr std::size_t size { 16 }; };
#       endif
#       ifdef __SSE2__
        template<unsigned N, typename = std::enable_if_t<N <= 2>> struct vector_size<N, double> { static constexpr std::size_t size { 16 }; };
        template<unsigned N, typename = std::enable_if_t<N <= 2>> struct vector_size<N, std::int64_t> { static constexpr std::size_t size { 16 }; };
        template<unsigned N, typename = std::enable_if_t<N <= 2>> struct vector_size<N, std::uint64_t> { static constexpr std::size_t size { 16 }; };
        template<unsigned N, typename = std::enable_if_t<N <= 4>> struct vector_size<N, std::int32_t> { static constexpr std::size_t size { 16 }; };
        template<unsigned N, typename = std::enable_if_t<N <= 4>> struct vector_size<N, std::uint32_t> { static constexpr std::size_t size { 16 }; };
        template<unsigned N, typename = std::enable_if_t<N <= 8>> struct vector_size<N, std::int16_t> { static constexpr std::size_t size { 16 }; };
        template<unsigned N, typename = std::enable_if_t<N <= 8>> struct vector_size<N, std::uint16_t> { static constexpr std::size_t size { 16 }; };
        template<unsigned N, typename = std::enable_if_t<N <= 16>> struct vector_size<N, std::int8_t> { static constexpr std::size_t size { 16 }; };
        template<unsigned N, typename = std::enable_if_t<N <= 16>> struct vector_size<N, std::uint8_t> { static constexpr std::size_t size { 16 }; };
#       endif

        struct empty { };
    }

    template <unsigned N, typename T>
    struct vector
    {
        using V [[gnu::vector_size(detail::vector_size<N, T>::size)]] = T;
        union
        {
            V v;
            std::array<T, N> a;
            struct
            {
                std::conditional_t<N >= 2 and N <= 4, T, detail::empty> x;
                std::conditional_t<N >= 2 and N <= 4, T, detail::empty> y;
                std::conditional_t<N >= 3 and N <= 4, T, detail::empty> z;
                std::conditional_t<N == 4, T, detail::empty> w;
            };
        };

        constexpr vector(V _v) noexcept : v(_v) { };

        template<typename... Ts>
        constexpr vector(Ts... args) noexcept : a { static_cast<T>(args)... } { }

        constexpr decltype(auto) operator[](std::ptrdiff_t i) const noexcept { return (a[i]); }

        constexpr vector() noexcept { };
        constexpr vector(const vector&) noexcept = default;
        constexpr vector(vector&&) noexcept = default;
        constexpr vector& operator=(const vector&) noexcept = default;
        constexpr vector& operator=(vector&&) noexcept = default;

        template <typename U> constexpr vector(const vector<N, U>& rhs) noexcept { for (unsigned i = 0; i < N; ++i) a[i] = static_cast<T>(rhs[i]); };

        template <typename U> constexpr vector& operator=(const vector<N, U>& rhs) noexcept { return *this = rhs.template cast<T>(); };
        template <typename U> constexpr vector& operator=(vector<N, U>&& rhs) noexcept { return *this = rhs.template cast<T>(); };

        template <typename U> constexpr vector<N, U> cast() const noexcept { return vector<N, U>{ std::is_integral<U>::value ? this->rounded() : *this }; }
        template <typename U> constexpr explicit operator vector<N, U>() const noexcept { return cast<U>(); }

        template <typename U> constexpr auto promoted() const noexcept { return vector<N, decltype(std::declval<T>() * std::declval<U>())> { *this }; }
        template <typename U> constexpr auto& operator+=(const vector<N, U>& rhs) noexcept { auto lhs = promoted<U>(); lhs.v += rhs.template promoted<T>().v; return *this = lhs; }
        template <typename U> constexpr auto& operator-=(const vector<N, U>& rhs) noexcept { return *this += -rhs; }

        template <typename U> constexpr vector& operator*=(const U& rhs) noexcept { auto lhs = promoted<U>(); lhs.v *= vector<N, U>{ rhs, rhs }.template promoted<T>().v; return *this = lhs; }
        template <typename U> constexpr vector& operator/=(const U& rhs) noexcept { auto lhs = promoted<U>(); lhs.v /= vector<N, U>{ rhs, rhs }.template promoted<T>().v; return *this = lhs; }

        //template <typename U> friend constexpr auto operator*(const vector& lhs, const vector<N, U>& rhs) { return lhs.x * rhs.x + lhs.y * rhs.y; }

        template <typename U> friend constexpr auto operator+(const vector& lhs, const vector<N, U>& rhs) noexcept { return lhs.promoted<U>() += rhs; }
        template <typename U> friend constexpr auto operator-(const vector& lhs, const vector<N, U>& rhs) noexcept { return lhs.promoted<U>() -= rhs; }
        template <typename U> friend constexpr auto operator*(const vector& lhs, const U& rhs) noexcept { return lhs.promoted<U>() *= rhs; }
        template <typename U> friend constexpr auto operator/(const vector& lhs, const U& rhs) noexcept { return lhs.promoted<U>() /= rhs; }
        template <typename U> friend constexpr auto operator*(const U& lhs, const vector& rhs) noexcept { return rhs * lhs; }
        template <typename U> friend constexpr auto operator/(const U& lhs, const vector& rhs) noexcept { return rhs / lhs; }
        constexpr auto operator-() const noexcept { return vector { -v }; }

        template <typename U> friend constexpr bool operator==(const vector& lhs, const vector<N, U>& rhs) noexcept { return rhs.v == lhs.v; }
        template <typename U> friend constexpr bool operator!=(const vector& lhs, const vector<N, U>& rhs) noexcept { return !(rhs == lhs); }

        constexpr auto square_magnitude() const noexcept
        {
            std::conditional_t<std::is_integral_v<T>, std::int64_t, double> result { };
            for (auto&& i : a) result += i * i;
            return result;
        }
        constexpr auto magnitude() const noexcept { return std::sqrt(static_cast<std::conditional_t<std::is_integral<T>::value, float, T>>(square_magnitude())); }
        constexpr auto length() const noexcept { return magnitude(); }

        template<typename U> constexpr auto angle(const vector<N, U>& other) const noexcept { return std::acos((*this * other) / (magnitude() * other.magnitude())); }
        constexpr auto angle() const noexcept { return angle(right()); }

        template<typename U> constexpr auto& scale(const vector<N, U>& other) noexcept { auto lhs = promoted<U>(); lhs.v *= other.template promoted<T>().v; return *this = lhs; }
        template<typename U> constexpr auto scaled(const vector<N, U>& other) const noexcept { return promoted<U>().scale(other); }

        constexpr auto& normalize() noexcept { return *this /= magnitude(); }
        constexpr auto normalized() const noexcept { return vector<N, decltype(std::declval<T>() / std::declval<decltype(magnitude())>())> { *this }.normalize(); }

        constexpr auto& round() noexcept { if constexpr (std::is_floating_point_v<T>) v = jw::round(v); return *this; }
        constexpr auto rounded() const noexcept { return vector { *this }.round(); }

        constexpr auto distance_from(const auto& other) const noexcept { return (*this - other).magnitude(); }

        constexpr vector& clamp_magnitude(const auto& max) noexcept
        {
            if (magnitude() > max)
            {
                normalize();
                *this *= max;
            }
            return *this;
        }

        constexpr auto clamped_magnitude(const auto& max) const noexcept
        {
            if (magnitude() <= max) return *this;
            auto copy = normalized();
            copy *= max;
            return copy;
        }

        constexpr vector& clamp(const vector& min, const vector& max) noexcept
        {
            for (unsigned i = 0; i < N; ++i)
            {
                a[i] = std::min(a[i], max.a[i]);
                a[i] = std::max(a[i], min.a[i]);
            }
            return *this;
        }

        template<typename U> constexpr auto clamped(const vector<N, U>& min, const vector<N, U>& max) const noexcept { return vector<N, U> { *this }.clamp(min, max); }

        constexpr auto sign() const noexcept
        {
            vector result;
            for (unsigned i = 0; i < N; ++i)
                result.a[i] = a[i] == 0 ? 0 : a[i] < 0 ? -1 : +1;
            return result;
        }

        template<typename U> constexpr auto& copysign(const vector<N, U>& other) noexcept
        {
            if constexpr (std::is_same_v<V, typename vector<N, U>::V>)
                v = jw::copysign(v, other.v);
            else
                for (unsigned i = 0; i < N; ++i) a[i] = jw::copysign(a[i], other.a[i]);
            return *this;
        }

        static constexpr auto up() { return vector { 0, -1 }; }
        static constexpr auto down() { return vector { 0,  1 }; }
        static constexpr auto left() { return vector { -1,  0 }; }
        static constexpr auto right() { return vector { 1,  0 }; }

        static constexpr auto distance(const auto& a, const auto& b) noexcept { return a.distance_from(b); }

        static constexpr auto max(const auto& a, const auto& b) noexcept
        {
            vector result;
            for (unsigned i = 0; i < N; ++i)
                result.a[i] = static_cast<T>(std::abs(a.a[i]) > std::abs(b.a[i]) ? a.a[i] : b.a[i]);
            return result;
        }

        static constexpr auto min(const auto& a, const auto& b) noexcept
        {
            vector result;
            for (unsigned i = 0; i < N; ++i)
                result.a[i] = static_cast<T>(std::abs(a.a[i]) < std::abs(b.a[i]) ? a.a[i] : b.a[i]);
            return result;
        }

        static constexpr auto max_abs(const auto& a, const auto& b) noexcept
        {
            vector result;
            for (unsigned i = 0; i < N; ++i)
                result.a[i] = std::max(static_cast<T>(a.a[i]), static_cast<T>(b.a[i]));
            return result;
        }

        static constexpr auto min_abs(const auto& a, const auto& b) noexcept
        {
            vector result;
            for (unsigned i = 0; i < N; ++i)
                result.a[i] = std::min(static_cast<T>(a.a[i]), static_cast<T>(b.a[i]));
            return result;
        }

        friend constexpr auto& operator<<(std::ostream& out, const vector& in)
        {
            out << '(';
            for (auto i = in.a.begin();;)
            {
                out << *i;
                if (++i != in.a.end()) out << ", ";
                else break;
            }
            out << ')';
            return out;
        }
    };

#   if defined(__SSE2__)
    using vector2f = vector<2, double>;
    using vector2i = vector<2, std::int32_t>;
    using vector3i = vector<3, std::int32_t>;
    using vector4i = vector<4, std::int32_t>;
#   else
    using vector2f = vector<2, float>;
    using vector2i = vector<2, std::int16_t>;
    using vector3i = vector<3, std::int16_t>;
    using vector4i = vector<4, std::int16_t>;
#   endif
    using vector3f = vector<3, float>;
    using vector4f = vector<4, float>;
}
