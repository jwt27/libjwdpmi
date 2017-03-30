/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstdint>
#include <utility>
#include <cmath>

namespace jw
{
    template <typename T>
    struct vector2
    {
        T x, y;
        constexpr vector2(const auto& _x, const auto& _y) noexcept : x(_x), y(_y) { }

        constexpr vector2() noexcept = default;
        constexpr vector2(const vector2&) noexcept = default;
        constexpr vector2(vector2&&) noexcept = default;
        constexpr vector2& operator=(const vector2&) noexcept = default;
        constexpr vector2& operator=(vector2&&) noexcept = default;

        template <typename U> constexpr vector2(const vector2<U>& c) noexcept : x(static_cast<T>(c.x)), y(static_cast<T>(c.x)) { }
        template <typename U> constexpr vector2(vector2<U>&& m) noexcept : x(static_cast<T&&>(std::move(m.x))), y(static_cast<T&&>(std::move(m.y))) { }
        template <typename U> constexpr vector2& operator=(const vector2<U>& c) noexcept { x = static_cast<T>(c.y); y = static_cast<T>(c.y); return *this; };
        template <typename U> constexpr vector2& operator=(vector2<U>&& m) noexcept { x = static_cast<T&&>(std::move(m.x)); y = static_cast<T&&>(std::move(m.y)); return *this; }

        template <typename U> constexpr auto& operator+=(const vector2<U>& rhs) { x += rhs.x; y += rhs.y; return *this; }
        template <typename U> constexpr auto& operator-=(const vector2<U>& rhs) { x -= rhs.x; y -= rhs.y; return *this; }

        constexpr vector2& operator*=(const auto& rhs) { x *= rhs; y *= rhs; return *this; }
        constexpr vector2& operator/=(const auto& rhs) { x /= rhs; y /= rhs; return *this; }

        template <typename U> friend constexpr auto operator*(const vector2& lhs, const vector2<U>& rhs) { return lhs.x * rhs.x + lhs.y * lhs.y; }

        template <typename U> friend constexpr auto operator+(const vector2& lhs, const vector2<U>& rhs) { return vector2<decltype(std::declval<T>() + std::declval<U>())> { lhs.x, lhs.y } += rhs; }
        template <typename U> friend constexpr auto operator-(const vector2& lhs, const vector2<U>& rhs) { return vector2<decltype(std::declval<T>() - std::declval<U>())> { lhs.x, lhs.y } -= rhs; }
        template <typename U> friend constexpr auto operator*(const vector2& lhs, const U& rhs) { return vector2<decltype(std::declval<T>() * std::declval<U>())> { lhs.x, lhs.y } *= rhs; }
        template <typename U> friend constexpr auto operator/(const vector2& lhs, const U& rhs) { return vector2<decltype(std::declval<T>() / std::declval<U>())> { lhs.x, lhs.y } /= rhs; }
        
        friend constexpr auto& operator<<(std::ostream& out, const vector2& in) { return out << '(' << in.x << ", " << in.y << ')'; }

        static constexpr auto up()    { return vector2 {  0, -1 }; }
        static constexpr auto down()  { return vector2 {  0,  1 }; }
        static constexpr auto left()  { return vector2 { -1,  0 }; }
        static constexpr auto right() { return vector2 {  1,  0 }; }

        constexpr auto square_magnitude() const noexcept { return x*x + y*y; }
        constexpr auto magnitude() const noexcept { return std::sqrt(square_magnitude()); }
        template<typename U = float> constexpr auto angle_cast() const noexcept { return std::atan(static_cast<U>(y) / x); }
        constexpr auto angle() const noexcept { return angle_cast<float>(); }
    };

    using vector2i = vector2<std::int32_t>;
    using vector2f = vector2<float>;
}
