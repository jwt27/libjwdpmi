/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <experimental/vector>
#include <jw/vector2.h>

namespace jw
{
    template<typename T>
    struct matrix 
    {
        using vector2s = vector2<std::size_t>;

        matrix(std::size_t w, std::size_t h, T* data) : p(data), dim(w, h) { }

        constexpr T& operator()(const vector2s& pos) { return *(p + pos.x + size().x * pos.y); }
        constexpr T& operator()(std::size_t x, std::size_t y) { return (*this)(vector2s { x, y }); }

        constexpr auto width() const noexcept { return size().x; }
        constexpr auto height() const noexcept { return size().y; }
        constexpr auto data_size() const noexcept { return width() * height(); }
        constexpr auto size() const noexcept { return dim; }

    protected:
        T* const p;
        const vector2s dim;
    };

    template<typename T>
    struct matrix_container : public matrix<T>
    {
        matrix_container(std::size_t w, std::size_t h, std::experimental::pmr::memory_resource* memres = std::experimental::pmr::get_default_resource()) 
            : matrix<T>(w, h, data.data()), data(w * h, std::experimental::pmr::polymorphic_allocator<T> { memres }) { }

    protected:
        std::experimental::pmr::vector<T> data;
    };

    template<typename T, std::size_t w, std::size_t h>
    struct fixed_matrix : public matrix<T> 
    {
        fixed_matrix() : matrix<T>(w, h, data.data()) { }

    protected:
        std::array<T, w * h> data;
    };
}
