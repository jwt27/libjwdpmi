/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstdint>
#include <utility>
#include <cmath>
#include <jw/math.h>

namespace jw
{
    template <std::size_t N, typename T>
    struct vector
    {
        using V [[gnu::vector_size(N * sizeof(T))]] = T;
        union
        {
            V v;
            std::array<T, N> a;
        };

        constexpr vector(V _v) noexcept : v(_v) { };

        template<typename... Ts>
        constexpr vector(Ts... args) noexcept : v { static_cast<T>(args)... } { }

        constexpr const T& operator[](std::ptrdiff_t i) const noexcept { return v[i]; }
        constexpr T& operator[](std::ptrdiff_t i) noexcept { return v[i]; }

        constexpr const T& at(std::size_t i) const { return a.at(i); }
        constexpr T& at(std::size_t i) { return a.at(i); }

        std::conditional_t<N >= 2 and N <= 4, T&, void> x() noexcept { return v[0]; }
        std::conditional_t<N >= 2 and N <= 4, T&, void> y() noexcept { return v[1]; }
        std::conditional_t<N >= 3 and N <= 4, T&, void> z() noexcept { return v[2]; }
        std::conditional_t<N == 4, T&, void> w() noexcept { return v[3]; }

        std::conditional_t<N >= 2 and N <= 4, const T&, void> x() const noexcept { return v[0]; }
        std::conditional_t<N >= 2 and N <= 4, const T&, void> y() const noexcept { return v[1]; }
        std::conditional_t<N >= 3 and N <= 4, const T&, void> z() const noexcept { return v[2]; }
        std::conditional_t<N == 4, const T&, void> w() const noexcept { return v[3]; }

        constexpr vector() noexcept : a { } { };
        constexpr vector(const vector&) noexcept = default;
        constexpr vector(vector&&) noexcept = default;
        constexpr vector& operator=(const vector&) noexcept = default;
        constexpr vector& operator=(vector&&) noexcept = default;

        template <typename U> constexpr vector(const vector<N, U>& rhs) noexcept
        {
            std::conditional_t<std::is_integral_v<T>, decltype(rhs.rounded()), const vector<N, U>&> rhs2 = [&rhs]
            {
                if constexpr (std::is_integral_v<T>) return rhs.rounded();
                else return rhs;
            }();
            for (unsigned i = 0; i < N; ++i) v[i] = static_cast<T>(rhs2[i]);
        }

        template <typename U> constexpr vector& operator=(const vector<N, U>& rhs) noexcept { return *this = rhs.template cast<T>(); };
        template <typename U> constexpr vector& operator=(vector<N, U>&& rhs) noexcept { return *this = rhs.template cast<T>(); };

        template <typename U, std::enable_if_t<std::is_same_v<U, T>, bool> = { }> constexpr vector& cast() noexcept { return *this; }
        template <typename U, std::enable_if_t<std::is_same_v<U, T>, bool> = { } > constexpr const vector& cast() const noexcept { return *this; }
        template <typename U, std::enable_if_t<not std::is_same_v<U, T>, bool> = { } > constexpr vector<N, U> cast() const noexcept { return vector<N, U>{ *this }; }
        template <typename U> constexpr explicit operator vector<N, U>() const noexcept { return cast<U>(); }

        template<typename U> using promoted_type =
            std::conditional_t<std::is_floating_point_v<T>, T,
            std::conditional_t<std::is_integral_v<T> and std::is_integral_v<U>,
            std::conditional_t<(sizeof(T) < sizeof(U)), T, U>, U>>;

        template <typename U, std::enable_if_t<not std::is_same_v<promoted_type<U>, T>, bool> = { }>
        constexpr auto promoted() const noexcept { return vector<N, promoted_type<U>> { *this }; }
        template <typename U, std::enable_if_t<std::is_same_v<promoted_type<U>, T>, bool> = { }>
        constexpr vector& promoted() noexcept { return *this; }
        template <typename U, std::enable_if_t<std::is_same_v<promoted_type<U>, T>, bool> = { } >
        constexpr const vector& promoted() const noexcept { return *this; }

        template<typename U> auto promote_scalar(const U& scalar) { return static_cast<promoted_type<U>>(scalar); }

        constexpr auto& operator+=(const vector& rhs) noexcept { v += rhs.v; return *this; }
        constexpr auto& operator-=(const vector& rhs) noexcept { return *this += -rhs; }

        constexpr vector& operator*=(const T& rhs) noexcept { v *= rhs; return *this; }
        constexpr vector& operator/=(const T& rhs) { v /= rhs; return *this; }

        friend constexpr auto operator+(vector lhs, const vector& rhs) noexcept { return lhs += rhs; }
        friend constexpr auto operator-(vector lhs, const vector& rhs) noexcept { return lhs -= rhs; }
        friend constexpr auto operator*(vector lhs, const T& rhs) noexcept { return lhs *= rhs; }
        friend constexpr auto operator/(vector lhs, const T& rhs) { return lhs /= rhs; }
        friend constexpr auto operator*(const T& lhs, const vector& rhs) noexcept { return rhs * lhs; }

        template <typename U> constexpr auto& operator+=(const vector<N, U>& rhs) noexcept { auto lhs = promoted<U>(); lhs.v += rhs.template promoted<T>().v; return *this = lhs; }
        template <typename U> constexpr auto& operator-=(const vector<N, U>& rhs) noexcept { return *this += -rhs; }

        template <typename U> constexpr vector& operator*=(const U& rhs) noexcept { auto lhs = promoted<U>(); lhs.v *= promote_scalar(rhs); return *this = lhs; }
        template <typename U> constexpr vector& operator/=(const U& rhs) { auto lhs = promoted<U>(); lhs.v /= promote_scalar(rhs); return *this = lhs; }

        template <typename U> friend constexpr auto operator+(vector lhs, const vector<N, U>& rhs) noexcept { return lhs += rhs; }
        template <typename U> friend constexpr auto operator-(vector lhs, const vector<N, U>& rhs) noexcept { return lhs -= rhs; }
        template <typename U> friend constexpr auto operator*(vector lhs, const U& rhs) noexcept { return lhs *= rhs; }
        template <typename U> friend constexpr auto operator/(vector lhs, const U& rhs) { return lhs /= rhs; }
        template <typename U> friend constexpr auto operator*(const U& lhs, const vector& rhs) noexcept { return rhs * lhs; }

        constexpr auto operator-() const noexcept { return vector { -v }; }

        template <typename U> constexpr friend bool operator==(const vector& lhs, const vector<N, U>& rhs) noexcept
        {
            auto result { lhs.template promoted<U>().v == rhs.template promoted<T>().v };
            if constexpr (N == 4) return (result[0] & result[1] & result[2] & result[3]) != 0;
            if constexpr (N == 3) return (result[0] & result[1] & result[2]) != 0;
            if constexpr (N == 2) return (result[0] & result[1]) != 0;
        }
        template <typename U> constexpr friend bool operator!=(const vector& lhs, const vector<N, U>& rhs) noexcept
        {
            auto result { lhs.template promoted<U>().v == rhs.template promoted<T>().v };
            if constexpr (N == 4) return (result[0] | result[1] | result[2] | result[3]) == 0;
            if constexpr (N == 3) return (result[0] | result[1] | result[2]) == 0;
            if constexpr (N == 2) return (result[0] | result[1]) == 0;
        }

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

        constexpr auto& normalize() { return *this /= magnitude(); }
        constexpr auto normalized() const { return vector<N, decltype(std::declval<T>() / std::declval<decltype(magnitude())>())> { *this }.normalize(); }

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
                v[i] = std::min(v[i], max[i]);
                v[i] = std::max(v[i], min[i]);
            }
            return *this;
        }

        template<typename U> constexpr auto clamped(const vector<N, U>& min, const vector<N, U>& max) const noexcept { return vector<N, U> { *this }.clamp(min, max); }

        constexpr vector& wrap(const vector& topleft, const vector& size)
        {
            *this -= topleft;
            for (unsigned i = 0; i < N; ++i)
            {
                if (std::abs(v[i]) >= size[i]) v[i] = jw::remainder(v[i], size[i]);
                if (v[i] < 0) v[i] += size[i];
            }
            *this += topleft;
            return *this;
        }

        template<typename U> constexpr auto wrapped(const vector<N, U>& topleft, const vector<N, U>& size) const noexcept { return vector<N, U> { *this }.wrap(topleft, size); }

        constexpr vector& wrap_abs(const vector& a, const vector& b)
        {
            auto min = min_abs(a, b);
            auto size = max_abs(a, b) + vector { 1,1 } - min;
            return wrap(min, size);
        }

        template<typename U> constexpr auto wrapped_abs(const vector<N, U>& a, const vector<N, U>& b) const noexcept { return vector<N, U> { *this }.wrap(a, b); }

        constexpr auto sign() const noexcept
        {
            vector result;
            for (unsigned i = 0; i < N; ++i)
                result[i] = v[i] == 0 ? 0 : v[i] < 0 ? -1 : +1;
            return result;
        }

        template<typename U> constexpr auto& copysign(const vector<N, U>& other) noexcept
        {
            if constexpr (std::is_same_v<V, typename vector<N, U>::V>)
                v = jw::copysign(v, other.v);
            else
                for (unsigned i = 0; i < N; ++i) v[i] = jw::copysign(v[i], other[i]);
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
                result[i] = static_cast<T>(std::abs(a[i]) > std::abs(b[i]) ? a[i] : b[i]);
            return result;
        }

        static constexpr auto min(const auto& a, const auto& b) noexcept
        {
            vector result;
            for (unsigned i = 0; i < N; ++i)
                result[i] = static_cast<T>(std::abs(a[i]) < std::abs(b[i]) ? a[i] : b[i]);
            return result;
        }

        static constexpr auto max_abs(const auto& a, const auto& b) noexcept
        {
            vector result;
            for (unsigned i = 0; i < N; ++i)
                result[i] = std::max(static_cast<T>(a[i]), static_cast<T>(b[i]));
            return result;
        }

        static constexpr auto min_abs(const auto& a, const auto& b) noexcept
        {
            vector result;
            for (unsigned i = 0; i < N; ++i)
                result[i] = std::min(static_cast<T>(a[i]), static_cast<T>(b[i]));
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
    using vector3i = vector<3, std::int32_t>;
    using vector4i = vector<4, std::int32_t>;
#   else
    using vector2f = vector<2, float>;
    using vector3i = vector<3, std::int16_t>;
    using vector4i = vector<4, std::int16_t>;
#   endif
    using vector2i = vector<2, std::int32_t>;
    using vector3f = vector<3, float>;
    using vector4f = vector<4, float>;
}

namespace std
{
    template<std::size_t N, typename T>
    jw::vector<N, T> abs(jw::vector<N, T> a)
    {
        for (unsigned i = 0; i < N; ++i) a[i] = std::abs(a[i]);
        return a;
    }

    template<std::size_t N, typename T>
    struct hash<jw::vector<N, T>>
    {
        using argument_type = jw::vector<N, T>;
        using result_type = std::size_t;
        constexpr result_type operator()(const argument_type& v) const noexcept
        {
            result_type seed { 0 };
            for (unsigned i = 0; i < N; ++i)
            {
                // algorithm from boost::hash_combine():
                // https://github.com/boostorg/container_hash/blob/f054fe932f4d5173bfd6dad5bcff5738a7aff0be/include/boost/container_hash/hash.hpp#L313
                seed ^= hash<T> { }(v[i]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            }
            return seed;
        }
    };
}
