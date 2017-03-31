/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <experimental/vector>
#include <type_traits>
#include <jw/vector2.h>

namespace jw
{
    template<typename M>
    struct matrix_range
    {
        constexpr matrix_range(M& matrix, const vector2i& position, const vector2i& dimensions) noexcept 
            : m(matrix), pos(position), dim(dimensions) { }

        constexpr auto make_range(const vector2i& position, const vector2i& dimensions) const noexcept { return matrix_range { m, position, dimensions }; }

        constexpr auto& operator()(vector2i p) noexcept { return get(p, m.data()); }
        constexpr const auto& operator()(vector2i p) const noexcept { return get(p, m.data()); }
        constexpr const auto& operator()(std::ptrdiff_t x, std::ptrdiff_t y) const noexcept { return (*this)({ x, y }); }
        constexpr auto& operator()(std::ptrdiff_t x, std::ptrdiff_t y) noexcept { return (*this)({ x, y }); }

        constexpr void fill(const auto& fill) noexcept
        {
            for (auto y = 0; y < height(); ++y)
                std::fill_n(&(*this)(0, y), width(), fill);
        }

        constexpr void assign(const auto& copy) noexcept
        {
            for (auto y = 0; y < std::min(height(), copy.height()); ++y)
                std::copy_n(&copy(0, y), std::min(width(), copy.width()), &(*this)(0, y));
        }

        constexpr auto size() const noexcept { return dim; }
        constexpr auto width() const noexcept { return size().x; }
        constexpr auto height() const noexcept { return size().y; }

    protected:
        constexpr auto& get(vector2i p, auto* ptr) const noexcept
        {
            p.x %= dim.x;
            p.y %= dim.y;
            if (p.x < 0) p.x = dim.x + p.x;
            if (p.y < 0) p.y = dim.y + p.y;
            auto offset = pos + p;
            return *(ptr + offset.x + m.size().x * offset.y);
        }

        M& m;
        const vector2i pos, dim;
    };

    template<typename R>
    struct matrix_iterator
    {
        constexpr matrix_iterator(R& range, const vector2i& pos) noexcept : r(range), p(pos) { }

        constexpr auto* operator->() noexcept { return &r(p); }
        constexpr const auto* operator->() const noexcept { return &r(p); }
        constexpr auto& operator*() noexcept { return r(p); }
        constexpr const auto& operator*() const noexcept { return r(p); }

        constexpr auto& operator+=(const auto& n) const noexcept { p += n; return *this; }
        constexpr auto& operator-=(const auto& n) const noexcept { p -= n; return *this; }

        R& r;
        vector2i p;
    };

    template<typename T>
    struct matrix : public matrix_range<matrix<T>>
    {
        constexpr matrix(std::size_t w, std::size_t h, T* data) : matrix_range<matrix<T>>(*this, { 0, 0 }, { w, h }), p(data) { }

        constexpr auto* data() noexcept { return p; }
        constexpr const auto* data() const noexcept { return data(); }
        constexpr auto data_size() const noexcept { return this->width() * this->height(); }

    protected:
        T* p;
    };

    template<typename T>
    struct matrix_container : public matrix<T>
    {
        matrix_container(std::size_t w, std::size_t h, std::experimental::pmr::memory_resource* memres = std::experimental::pmr::get_default_resource()) 
            : matrix<T>(w, h, nullptr), data(w * h, std::experimental::pmr::polymorphic_allocator<T> { memres }) { this->p = data.data(); }

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
