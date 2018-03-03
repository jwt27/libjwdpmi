/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstdint>
#include <utility>
#include <cmath>
#include <jw/math.h>

namespace jw
{
    template <unsigned N, typename T>
    struct vector
    {
        using V [[gnu::vector_size(2 * sizeof(T))]] = T;
        union
        {
            V v;
            struct { T x, y; };
        };

        constexpr vector(const auto& _x, const auto& _y) noexcept : x(_x), y(_y) { }

        constexpr vector() noexcept = default;
        constexpr vector(const vector&) noexcept = default;
        constexpr vector(vector&&) noexcept = default;
        constexpr vector& operator=(const vector&) noexcept = default;
        constexpr vector& operator=(vector&&) noexcept = default;

        template <typename U> constexpr vector(const vector<N, U>& c) noexcept : x(static_cast<T>(c.x)), y(static_cast<T>(c.y)) { }
        template <typename U> constexpr vector(vector<N, U>&& m) noexcept : x(static_cast<T&&>(m.x)), y(static_cast<T&&>(m.y)) { }
        template <typename U> constexpr vector& operator=(const vector<N, U>& rhs) noexcept { return *this = rhs.template cast<T>(); };
        template <typename U> constexpr vector& operator=(vector<N, U>&& rhs) noexcept { return *this = rhs.template cast<T>(); };

        template <typename U> constexpr vector<N, U> cast() const noexcept { return vector<N, U>{ std::is_integral<U>::value ? this->rounded() : *this }; }
        template <typename U> constexpr explicit operator vector<N, U>() const noexcept { return cast<U>(); }

        template <typename U> constexpr auto promoted() const noexcept { return vector<N, decltype(std::declval<T>() * std::declval<U>())> { *this }; }

        template <typename U> constexpr auto& operator+=(const vector<N, U>& rhs) noexcept { auto lhs = promoted<U>(); lhs.v += rhs.template promoted<T>().v; return *this = lhs; }
        template <typename U> constexpr auto& operator-=(const vector<N, U>& rhs) noexcept { return *this += -rhs; }

        template <typename U> constexpr vector& operator*=(const U& rhs) noexcept { auto lhs = promoted<U>(); lhs.v *= vector<N, U>{ rhs, rhs }.template promoted<T>().v; return *this = lhs; }
        template <typename U> constexpr vector& operator/=(const U& rhs) noexcept { auto lhs = promoted<U>(); lhs.v /= vector<N, U>{ rhs, rhs }.template promoted<T>().v; return *this = lhs; }

        template <typename U> friend constexpr auto operator*(const vector& lhs, const vector<N, U>& rhs) { return lhs.x * rhs.x + lhs.y * rhs.y; }

        template <typename U> friend constexpr auto operator+(const vector& lhs, const vector<N, U>& rhs) noexcept { return lhs.promoted<U>() += rhs; }
        template <typename U> friend constexpr auto operator-(const vector& lhs, const vector<N, U>& rhs) noexcept { return lhs.promoted<U>() -= rhs; }
        template <typename U> friend constexpr auto operator*(const vector& lhs, const U& rhs) noexcept { return lhs.promoted<U>() *= rhs; }
        template <typename U> friend constexpr auto operator/(const vector& lhs, const U& rhs) noexcept { return lhs.promoted<U>() /= rhs; }
        template <typename U> friend constexpr auto operator*(const U& lhs, const vector& rhs) noexcept { return rhs * lhs; }
        template <typename U> friend constexpr auto operator/(const U& lhs, const vector& rhs) noexcept { return rhs / lhs; }
        constexpr auto operator-() const noexcept { return vector { -x, -y }; }

        template <typename U> friend constexpr bool operator==(const vector& lhs, const vector<N, U>& rhs) noexcept { return rhs.x == lhs.x && rhs.y == lhs.y; }
        template <typename U> friend constexpr bool operator!=(const vector& lhs, const vector<N, U>& rhs) noexcept { return !(rhs == lhs); }
        
        friend constexpr auto& operator<<(std::ostream& out, const vector& in) { return out << '(' << in.x << ", " << in.y << ')'; }

        constexpr auto square_magnitude() const noexcept { return x*x + y*y; }
        constexpr auto magnitude() const noexcept { return std::sqrt(static_cast<std::conditional_t<std::is_integral<T>::value, float, T>>(square_magnitude())); }
        constexpr auto length() const noexcept { return magnitude(); }

        template<typename U> constexpr auto angle(const vector<N, U>& other) const noexcept { return std::acos((*this * other) / (magnitude() * other.magnitude())); }
        constexpr auto angle() const noexcept { return angle(right()); }

        template<typename U> constexpr auto& scale(const vector<N, U>& other) noexcept { auto lhs = promoted<U>(); lhs.v *= other.template promoted<T>().v; return *this = lhs; }
        template<typename U> constexpr auto scaled(const vector<N, U>& other) const noexcept { return promoted<U>().scale(other); }

        constexpr auto& normalize() noexcept { return *this /= magnitude(); }
        constexpr auto normalized() const noexcept { return vector<N, decltype(std::declval<T>() / std::declval<decltype(magnitude())>())> { *this }.normalize(); }

        constexpr auto& round() noexcept { v = jw::round(v); return *this; }
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
            x = std::min(x, max.x);
            x = std::max(x, min.x);
            y = std::min(y, max.y);
            y = std::max(y, min.y);
            return *this;
        }

        template<typename U> constexpr auto clamped(const vector<N, U>& min, const vector<N, U>& max) const noexcept { return vector<N, U> { *this }.clamp(min, max); }

        constexpr auto sign() const noexcept 
        {
            auto sign_x = x == 0 ? 0 : x < 0 ? -1 : 1;
            auto sign_y = x == 0 ? 0 : x < 0 ? -1 : 1;
            return vector { sign_x, sign_y };
        }

        template<typename U> constexpr auto& copysign(const vector<N, U>& other) noexcept
        {
            v = jw::copysign(promoted<U>().v, other.template promoted<T>().v);
            return *this;
        }

        static constexpr auto up()    { return vector {  0, -1 }; }
        static constexpr auto down()  { return vector {  0,  1 }; }
        static constexpr auto left()  { return vector { -1,  0 }; }
        static constexpr auto right() { return vector {  1,  0 }; }

        static constexpr auto distance(const auto& a, const auto& b) noexcept { return a.distance_from(b); }

        static constexpr auto max(const auto& a, const auto& b) noexcept
        {
            auto max_x = static_cast<T>(std::abs(a.x) > std::abs(b.x) ? a.x : b.x);
            auto max_y = static_cast<T>(std::abs(a.y) > std::abs(b.y) ? a.y : b.y);
            return vector { max_x, max_y }; 
        }

        static constexpr auto min(const auto& a, const auto& b) noexcept
        {
            auto min_x = static_cast<T>(std::abs(a.x) < std::abs(b.x) ? a.x : b.x);
            auto min_y = static_cast<T>(std::abs(a.y) < std::abs(b.y) ? a.y : b.y);
            return vector { min_x, min_y };
        }

        static constexpr auto max_abs(const auto& a, const auto& b) noexcept
        {
            auto max_x = std::max(static_cast<T>(a.x), static_cast<T>(b.x));
            auto max_y = std::max(static_cast<T>(a.y), static_cast<T>(b.y));
            return vector { max_x, max_y }; 
        }

        static constexpr auto min_abs(const auto& a, const auto& b) noexcept
        {
            auto min_x = std::min(static_cast<T>(a.x), static_cast<T>(b.x));
            auto min_y = std::min(static_cast<T>(a.y), static_cast<T>(b.y));
            return vector { min_x, min_y };
        }
    };

    using vector2i = vector<2, std::int32_t>;
    using vector2f = vector<2, float>;
}
