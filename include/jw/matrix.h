/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <experimental/vector>
#include <type_traits>
#include <jw/vector.h>

namespace jw
{
    enum class matrix_iterator_direction { up, down, left, right };
    struct invalid_matrix_iterator { };

    template<typename R, matrix_iterator_direction D>
    struct matrix_iterator
    {
        constexpr matrix_iterator(R& range, const vector2i& pos) noexcept : r(range), p(pos) { }

        constexpr matrix_iterator(const matrix_iterator&) noexcept = default;
        constexpr matrix_iterator(matrix_iterator&&) noexcept = default;
        constexpr matrix_iterator& operator=(const matrix_iterator&) noexcept = default;
        constexpr matrix_iterator& operator=(matrix_iterator&&) noexcept = default;

        constexpr auto* operator->() noexcept { return &r.at(p); }
        constexpr const auto* operator->() const noexcept { return &r.at(p); }
        constexpr auto& operator*() noexcept { return r.at(p); }
        constexpr const auto& operator*() const noexcept { return r.at(p); }
        constexpr auto& operator[](std::ptrdiff_t n) const { return *(matrix_iterator { *this } += n); }

        constexpr auto& operator-=(const vector2i& n) const { p -= n; return *this; }   // TODO: rotate
        constexpr auto& operator+=(const vector2i& n) const { p += n; return *this; }

        constexpr auto& operator-=(std::ptrdiff_t n) const { return *this += -n; }
        constexpr auto& operator+=(std::ptrdiff_t n) const 
        {
            p += n * direction();
            if constexpr (D == matrix_iterator_direction::right) if (p.x >= r.width())
            {
                p.y += p.x / r.width();
                p.x %= r.width();
            }
            if constexpr (D == matrix_iterator_direction::left) if (p.x < 0)
            {
                p.y -= std::abs(p.x / r.width()) + 1;
                p.x = r.width() - (std::abs(p.x + 1) % r.width()) - 1;
            }
            if constexpr (D == matrix_iterator_direction::down) if (p.y >= r.height())
            {
                p.x += p.y / r.height();
                p.y %= r.height();
            }
            if constexpr (D == matrix_iterator_direction::up) if (p.y < 0)
            {
                p.x -= std::abs(p.y / r.height()) + 1;
                p.y = r.height() - (std::abs(p.y + 1) % r.height()) - 1;
            }
            return *this;
        }

        constexpr auto& operator++() const noexcept { return *this += 1; }
        constexpr auto& operator++(int) const noexcept { auto copy = matrix_iterator { *this }; *this += 1; return copy; }

        constexpr bool invalid() const noexcept { return (p.x < 0 or p.x >= r.width() or p.y < 0 or p.y >= r.height()); }
        constexpr vector2i direction() const noexcept
        {
            if constexpr (D == matrix_iterator_direction::up) return vector2i::up();
            if constexpr (D == matrix_iterator_direction::down) return vector2i::down();
            if constexpr (D == matrix_iterator_direction::left) return vector2i::left();
            if constexpr (D == matrix_iterator_direction::right) return vector2i::right();
        }

        template<typename R2, matrix_iterator_direction D2>
        friend constexpr bool operator==(const matrix_iterator& lhs, const matrix_iterator<R2, D2>& rhs) noexcept { return &(*lhs) == &(*rhs); }
        template<typename R2, matrix_iterator_direction D2>
        friend constexpr bool operator!=(const matrix_iterator& lhs, const matrix_iterator<R2, D2>& rhs) noexcept { return !(lhs == rhs); }

        friend constexpr bool operator==(const matrix_iterator& lhs, const invalid_matrix_iterator&) noexcept { return lhs.invalid(); }
        friend constexpr bool operator!=(const matrix_iterator& lhs, const invalid_matrix_iterator&) noexcept { return not lhs.invalid(); }

        R& r;
        mutable vector2i p;
    };

    template<typename R, typename T, unsigned L>
    struct matrix_range
    {
        using iterator = matrix_iterator<matrix_range, matrix_iterator_direction::right>;
        using reverse_iterator = matrix_iterator<matrix_range, matrix_iterator_direction::left>;
        using vertical_iterator = matrix_iterator<matrix_range, matrix_iterator_direction::down>;
        using reverse_vertical_iterator = matrix_iterator<matrix_range, matrix_iterator_direction::up>;
        using const_iterator = matrix_iterator<const matrix_range, matrix_iterator_direction::right>;
        using const_reverse_iterator = matrix_iterator<const matrix_range, matrix_iterator_direction::left>;
        using const_vertical_iterator = matrix_iterator<const matrix_range, matrix_iterator_direction::down>;
        using const_reverse_vertical_iterator = matrix_iterator<const matrix_range, matrix_iterator_direction::up>;

        template<typename, typename, unsigned> friend struct matrix_range;

        template<typename R2>
        constexpr matrix_range(R2&& range, const vector2i& position, const vector2i& dimensions) noexcept 
            : r(std::forward<R2>(range)), pos(position), dim(dimensions) { }

        constexpr matrix_range() noexcept = delete;
        constexpr matrix_range(const matrix_range&) noexcept = default;
        constexpr matrix_range(matrix_range&&) noexcept = default;
        constexpr matrix_range& operator=(const matrix_range&) noexcept = delete;
        constexpr matrix_range& operator=(matrix_range&&) noexcept = default;

        constexpr auto range(vector2i position, vector2i dimensions) const noexcept { return make_range(*this, position, dimensions); }
        constexpr auto range(vector2i position, vector2i dimensions) noexcept { return make_range(*this, position, dimensions); }

        constexpr auto range_abs(const vector2i& topleft, const vector2i& bottomright) const noexcept { return range(topleft, bottomright - topleft); }
        constexpr auto range_abs(const vector2i& topleft, const vector2i& bottomright) noexcept { return range(topleft, bottomright - topleft); }

        constexpr const auto& operator()(vector2i p) const noexcept { return get_wrap(p); }
        constexpr auto& operator()(vector2i p) noexcept { return remove_const(get_wrap(p)); }
        constexpr const auto& operator()(std::ptrdiff_t x, std::ptrdiff_t y) const noexcept { return (*this)({ x, y }); }
        constexpr auto& operator()(std::ptrdiff_t x, std::ptrdiff_t y) noexcept { return (*this)({ x, y }); }

        constexpr const auto& at(vector2i p) const noexcept { return get_wrap(p); }
        constexpr auto& at(vector2i p) noexcept { return remove_const(get_wrap(p)); }
        constexpr const auto& at(std::ptrdiff_t x, std::ptrdiff_t y) const noexcept { return at({ x, y }); }
        constexpr auto& at(std::ptrdiff_t x, std::ptrdiff_t y) noexcept { return at({ x, y }); }

        constexpr const auto& nowrap(vector2i p) const noexcept { return get(p, r); }
        constexpr auto& nowrap(vector2i p) noexcept { return remove_const(get(p)); }
        constexpr const auto& nowrap(std::ptrdiff_t x, std::ptrdiff_t y) const noexcept { return nowrap({ x, y }); }
        constexpr auto& nowrap(std::ptrdiff_t x, std::ptrdiff_t y) noexcept { return nowrap({ x, y }); }

        template<typename R2, unsigned L2> friend constexpr bool operator==(const matrix_range& lhs, const matrix_range<R2, T, L2>& rhs) noexcept
        {
            return lhs.matrix().data() == rhs.matrix().data() and &lhs(0, 0) == &rhs(0, 0) and
                &lhs(lhs.size() - vector2i { 1, 1 }) == &rhs(rhs.size() - vector2i { 1, 1 });
        }
        template<typename R2, unsigned L2> friend constexpr bool operator!=(const matrix_range& lhs, const matrix_range<R2, T, L2>& rhs) noexcept { return not (lhs == rhs); }

        constexpr matrix_range& fill(const auto& fill)
        {
            for (auto &&i : *this) i = fill;
            return *this;
        }

        constexpr matrix_range& assign(const auto& copy)
        {
            auto size = vector2i::min(this->size(), copy.size());
            for (auto y = 0; y < size[1]; ++y)
                for (auto x = 0; x < size[0]; ++x)
                    at(x, y) = copy.at(x, y);
            return *this;
        }

        constexpr auto begin() noexcept   { return iterator { *this, { 0, 0 } }; }
        constexpr auto vbegin() noexcept  { return vertical_iterator { *this, { 0, 0 } }; }
        constexpr auto rbegin() noexcept  { return vertical_iterator { *this, size() - vector2i { 1, 1 } }; }
        constexpr auto rvbegin() noexcept { return reverse_vertical_iterator { *this, size() - vector2i { 1, 1 } }; }
        constexpr auto cbegin() const noexcept   { return const_iterator { *this, { 0, 0 } }; }
        constexpr auto cvbegin() const noexcept  { return const_vertical_iterator { *this, { 0, 0 } }; }
        constexpr auto crbegin() const noexcept  { return const_vertical_iterator { *this, size() - vector2i { 1, 1 } }; }
        constexpr auto crvbegin() const noexcept { return const_reverse_vertical_iterator { *this, size() - vector2i { 1, 1 } }; }
        constexpr auto end() const noexcept { return invalid_matrix_iterator { }; }

        [[gnu::const]] constexpr const auto& position() const noexcept { return pos; }
        [[gnu::const]] constexpr auto size() const noexcept { return std::abs(dim); }
        [[gnu::const]] constexpr auto width() const noexcept { return size().x; }
        [[gnu::const]] constexpr auto height() const noexcept { return size().y; }

        template<unsigned level = L> constexpr auto& matrix() const noexcept
        {
            if constexpr (level == 0) return r;
            else return r.template matrix<level - 1>();
        }

    protected:
        template<typename Self>
        constexpr auto make_range(Self&& self, vector2i position, vector2i dimensions) const noexcept
        {
            auto dim_sign = dim.sign();
            dimensions[0] *= dim_sign[0]; dimensions[1] *= dim_sign[1];
            return matrix_range<std::remove_reference_t<Self>, T, L + 1> { std::forward<Self>(self), position, dimensions };
        }

        constexpr T& remove_const(const T& t) const noexcept { return const_cast<T&>(t); }

        constexpr const T& get_wrap(vector2i p) const
        {
            auto s = this->size();
            p.v %= s.v;
            p += s;
            p.v %= s.v;
            p.copysign(dim);
            p += pos;
            if constexpr (L > 0) return r.get_wrap(p);
            else return r.base_get(p);
        }

        constexpr const T& get(vector2i p) const
        {
            p.copysign(dim);
            p += pos;
            if constexpr (L > 0) return r.get(p);
            else return r.base_get(p);
        }

        std::conditional_t<(L < 2), R&, R> r;
        vector2i pos, dim;
    };

    template<typename T>
    struct matrix : public matrix_range<matrix<T>, T, 0>
    {
        template<typename, typename, unsigned> friend struct matrix_range;

        constexpr matrix(vector2i size, T* data) : matrix_range<matrix<T>, T, 0>(*this, { 0, 0 }, size), ptr(data) { }
        constexpr matrix(std::size_t w, std::size_t h, T* data) : matrix(vector2i { w, h }, data) { }

        constexpr auto* data() noexcept { return ptr; }
        constexpr const auto* data() const noexcept { return data(); }
        constexpr auto data_size() const noexcept { return this->width() * this->height(); }

    protected:
        constexpr const T& base_get(vector2i p) const
        {
            return *(ptr + p[0] + this->size()[0] * p[1]);
        }

        T* ptr;
    };

    template<typename T>
    struct matrix_container : public matrix<T>
    {
        matrix_container(vector2i size, std::experimental::pmr::memory_resource* memres = std::experimental::pmr::get_default_resource()) 
            : matrix<T>(size, nullptr), data(size.x * size.y, std::experimental::pmr::polymorphic_allocator<T> { memres }) { this->ptr = data.data(); }
        matrix_container(std::size_t w, std::size_t h, std::experimental::pmr::memory_resource* memres = std::experimental::pmr::get_default_resource())
            : matrix_container(vector2i { w,h }, memres) { }

    protected:
        std::experimental::pmr::vector<T> data;
    };

    template<typename T, std::size_t w, std::size_t h>
    struct fixed_matrix : public matrix<T> 
    {
        constexpr fixed_matrix() : matrix<T>(w, h, array.data()) { }

    protected:
        std::array<T, w * h> array;
    };
}
