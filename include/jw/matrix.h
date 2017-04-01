/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <experimental/vector>
#include <type_traits>
#include <jw/vector2.h>

namespace jw
{
    template<typename R>
    struct matrix_iterator
    {
        constexpr matrix_iterator(R& range, const vector2i& pos) noexcept : r(range), p(pos) { }

        constexpr matrix_iterator(const matrix_iterator&) noexcept = default;
        constexpr matrix_iterator(matrix_iterator&&) noexcept = default;
        constexpr matrix_iterator& operator=(const matrix_iterator&) noexcept = default;
        constexpr matrix_iterator& operator=(matrix_iterator&&) noexcept = default;

        constexpr auto* operator->() noexcept { return &r.get(p); }
        constexpr const auto* operator->() const noexcept { return &r.get(p); }
        constexpr auto& operator*() noexcept { return r.get(p); }
        constexpr const auto& operator*() const noexcept { return r.get(p); }
        constexpr auto& operator[](std::ptrdiff_t n) const noexcept  { return *(matrix_iterator { *this } += n); }

        constexpr auto& operator-=(const vector2i& n) const noexcept { p -= n; return *this; }
        constexpr auto& operator+=(const vector2i& n) const noexcept { p += n; return *this; }

        constexpr auto& operator-=(std::ptrdiff_t n) const noexcept { return *this += -n; }
        constexpr auto& operator+=(std::ptrdiff_t n) const noexcept 
        {
            p.x += n;
            if (p.x > r.width())
            {
                p.y += p.x / r.width();
                p.x %= r.width();
            }
            return *this;
        }

        constexpr auto& operator++() const noexcept { return *this += 1; }
        constexpr auto& operator++(int) const noexcept { auto copy = matrix_iterator { *this }; *this += 1; return copy; }

        friend constexpr bool operator==(const matrix_iterator& lhs, const matrix_iterator& rhs) noexcept { return &(*lhs) == &(*rhs); }
        friend constexpr bool operator!=(const matrix_iterator& lhs, const matrix_iterator& rhs) noexcept { return !(lhs == rhs); }

        R& r;
        mutable vector2i p;
    };

    template<typename M>
    struct matrix_range
    {
        using iterator = matrix_iterator<matrix_range>;
        friend iterator;

        constexpr matrix_range(M& matrix, const vector2i& position, const vector2i& dimensions) noexcept 
            : m(matrix), pos(position), dim(dimensions) { }

        constexpr auto range(const vector2i& position, const vector2i& dimensions) const noexcept
        {
            auto new_pos = pos + vector2i::max_abs(position, vector2i { 0,0 });
            auto new_dim = vector2i::min(dimensions, dim - new_pos).copysign(dimensions);
            return matrix_range { m, pos + new_pos, new_dim };
        }

        // bounds checking with automatic wrap-around
        constexpr auto& operator()(vector2i p) noexcept { return get_wrap(p, m.data()); }
        constexpr const auto& operator()(vector2i p) const noexcept { return get_wrap(p, m.data()); }
        constexpr const auto& operator()(std::ptrdiff_t x, std::ptrdiff_t y) const noexcept { return (*this)({ x, y }); }
        constexpr auto& operator()(std::ptrdiff_t x, std::ptrdiff_t y) noexcept { return (*this)({ x, y }); }

        // no bounds checking
        constexpr const auto& get(vector2i p) const noexcept { return get(p, m.data()); }
        constexpr auto& get(vector2i p) noexcept { return get(p, m.data()); }
        constexpr const auto& get(std::ptrdiff_t x, std::ptrdiff_t y) const noexcept { return get({ x, y }); }
        constexpr auto& get(std::ptrdiff_t x, std::ptrdiff_t y) noexcept { return get({ x, y }); }

        friend constexpr bool operator==(const matrix_range& lhs, const matrix_range& rhs) noexcept { return lhs.m.data() == rhs.m.data() && lhs.pos == rhs.pos && lhs.dim == rhs.dim; }
        friend constexpr bool operator!=(const matrix_range& lhs, const matrix_range& rhs) noexcept { return !(lhs == rhs); }

        constexpr void fill(const auto& fill) noexcept
        {
            for (auto y = 0; y < height(); ++y)
                std::fill_n(&(*this).get(0, y), width(), fill);
        }

        constexpr void assign(const auto& copy) noexcept
        {
            for (auto y = 0; y < std::min(height(), copy.height()); ++y)
                std::copy_n(&copy.get(0, y), std::min(width(), copy.width()), &(*this).get(0, y));
        }

        constexpr auto begin() noexcept { return iterator { *this, { 0, 0 }}; }
        constexpr auto end() noexcept { return iterator { *this, dim }; }

        constexpr auto size() const noexcept { return dim; }
        constexpr auto width() const noexcept { return size().x; }
        constexpr auto height() const noexcept { return size().y; }

    protected:
        constexpr auto& get_wrap(vector2i p, auto* ptr) const noexcept
        {
            p.x %= dim.x;
            p.y %= dim.y;
            if (p.x < 0) p.x = dim.x + p.x;
            if (p.y < 0) p.y = dim.y + p.y;
            auto offset = pos + p;
            return *(ptr + offset.x + m.size().x * offset.y);
        }

        constexpr auto& get(vector2i p, auto* ptr) const noexcept
        {
            auto offset = pos + p;
            return *(ptr + offset.x + m.size().x * offset.y);
        }

        M& m;
        const vector2i pos, dim;
    };

    template<typename T>
    struct matrix : public matrix_range<matrix<T>>
    {
        constexpr matrix(vector2i size, T* data) : matrix_range<matrix<T>>(*this, { 0, 0 }, size), p(data) { }
        constexpr matrix(std::size_t w, std::size_t h, T* data) : matrix(vector2i { w, h }, data) { }

        constexpr auto* data() noexcept { return p; }
        constexpr const auto* data() const noexcept { return data(); }
        constexpr auto data_size() const noexcept { return this->width() * this->height(); }

    protected:
        T* p;
    };

    template<typename T>
    struct matrix_container : public matrix<T>
    {
        matrix_container(vector2i size, std::experimental::pmr::memory_resource* memres = std::experimental::pmr::get_default_resource()) 
            : matrix<T>(size, nullptr), data(size.x * size.y, std::experimental::pmr::polymorphic_allocator<T> { memres }) { this->p = data.data(); }
        matrix_container(std::size_t w, std::size_t h, std::experimental::pmr::memory_resource* memres = std::experimental::pmr::get_default_resource())
            : matrix_container(vector2i { w,h }, memres) { }

    protected:
        std::experimental::pmr::vector<T> data;
    };

    template<typename T, std::size_t w, std::size_t h>
    struct fixed_matrix : public matrix<T> 
    {
        constexpr fixed_matrix() : matrix<T>(w, h, data.data()) { }

    protected:
        std::array<T, w * h> data;
    };
}
