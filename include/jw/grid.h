/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <vector>
#include <type_traits>
#include <memory_resource>
#include <jw/vector.h>

namespace jw
{
    enum class grid_iterator_direction { up, down, left, right };
    struct invalid_grid_iterator { };

    template<typename R, grid_iterator_direction D>
    struct grid_iterator
    {
        constexpr grid_iterator(R& range, const vector2i& pos) noexcept : r(range), p(pos) { }

        constexpr grid_iterator(const grid_iterator&) noexcept = default;
        constexpr grid_iterator(grid_iterator&&) noexcept = default;
        constexpr grid_iterator& operator=(const grid_iterator&) noexcept = default;
        constexpr grid_iterator& operator=(grid_iterator&&) noexcept = default;

        constexpr auto* operator->() noexcept { return &r.remove_const(r.get_maybe_wrap(p)); }
        constexpr const auto* operator->() const noexcept { return &r.get_maybe_wrap(p); }
        constexpr auto& operator*() noexcept { return r.remove_const(r.get_maybe_wrap(p)); }
        constexpr const auto& operator*() const noexcept { return r.get_maybe_wrap(p); }

        constexpr auto& operator[](std::ptrdiff_t n) const { return *(grid_iterator { *this } += n); }

        constexpr auto& operator-=(const vector2i& n) const { return *this += -n; }
        constexpr auto& operator+=(const vector2i& n) const
        {
            p += rotate(n);
            check_overflow();
            return *this;
        }

        constexpr auto& operator-=(std::ptrdiff_t n) const { return *this += -n; }
        constexpr auto& operator+=(std::ptrdiff_t n) const
        {
            p += n * direction();
            check_overflow();
            return *this;
        }

        constexpr auto& operator++(int) const noexcept { auto copy = grid_iterator { *this }; ++(*this); return copy; }
        constexpr auto& operator++() const noexcept
        {
            p += direction();
            if constexpr (D == grid_iterator_direction::right) if (p.x() >= r.width()) { p.y() += 1; p.x() = 0; }
            if constexpr (D == grid_iterator_direction::left) if (p.x() < 0) { p.y() -= 1; p.x() = r.width() - 1; }
            if constexpr (D == grid_iterator_direction::down) if (p.y() >= r.height()) { p.x() += 1; p.y() = 0; }
            if constexpr (D == grid_iterator_direction::up) if (p.y() < 0) { p.x() -= 1; p.y() = r.height() - 1; }
            return *this;
        }

        constexpr bool valid() const noexcept { return p.x() + p.y() * r.width() < r.width() * r.height(); }
        constexpr const auto& position() const noexcept { return p; }

        constexpr vector2i direction() const noexcept
        {
            if constexpr (D == grid_iterator_direction::up) return vector2i::up();
            if constexpr (D == grid_iterator_direction::down) return vector2i::down();
            if constexpr (D == grid_iterator_direction::left) return vector2i::left();
            if constexpr (D == grid_iterator_direction::right) return vector2i::right();
        }

        template<typename R2, grid_iterator_direction D2>
        friend constexpr bool operator==(const grid_iterator& lhs, const grid_iterator<R2, D2>& rhs) noexcept { return &(*lhs) == &(*rhs); }
        template<typename R2, grid_iterator_direction D2>
        friend constexpr bool operator!=(const grid_iterator& lhs, const grid_iterator<R2, D2>& rhs) noexcept { return !(lhs == rhs); }

        friend constexpr bool operator==(const grid_iterator& lhs, const invalid_grid_iterator&) noexcept { return not lhs.valid(); }
        friend constexpr bool operator!=(const grid_iterator& lhs, const invalid_grid_iterator&) noexcept { return lhs.valid(); }

    protected:
        constexpr vector2i rotate(const vector2i& v) const noexcept
        {
            if constexpr (D == grid_iterator_direction::up) return vector2i { -v[1], -v[0] };
            if constexpr (D == grid_iterator_direction::down) return vector2i { v[1], v[0] };
            if constexpr (D == grid_iterator_direction::left) return -v;
            if constexpr (D == grid_iterator_direction::right) return v;
        }

        constexpr void check_overflow() const
        {
            if constexpr (D == grid_iterator_direction::right) if (p.x() >= r.width())
            {
                p.y() += p.x() / r.width();
                p.x() %= r.width();
            }
            if constexpr (D == grid_iterator_direction::left) if (p.x() < 0)
            {
                p.y() -= std::abs(p.x() / r.width()) + 1;
                p.x() = r.width() - (std::abs(p.x() + 1) % r.width()) - 1;
            }
            if constexpr (D == grid_iterator_direction::down) if (p.y() >= r.height())
            {
                p.x() += p.y() / r.height();
                p.y() %= r.height();
            }
            if constexpr (D == grid_iterator_direction::up) if (p.y() < 0)
            {
                p.x() -= std::abs(p.y() / r.height()) + 1;
                p.y() = r.height() - (std::abs(p.y() + 1) % r.height()) - 1;
            }
        }

        R& r;
        mutable vector2i p;
    };

    template<typename R, typename T, unsigned L>
    struct grid_range
    {
        using iterator = grid_iterator<grid_range, grid_iterator_direction::right>;
        using reverse_iterator = grid_iterator<grid_range, grid_iterator_direction::left>;
        using vertical_iterator = grid_iterator<grid_range, grid_iterator_direction::down>;
        using reverse_vertical_iterator = grid_iterator<grid_range, grid_iterator_direction::up>;
        using const_iterator = grid_iterator<const grid_range, grid_iterator_direction::right>;
        using const_reverse_iterator = grid_iterator<const grid_range, grid_iterator_direction::left>;
        using const_vertical_iterator = grid_iterator<const grid_range, grid_iterator_direction::down>;
        using const_reverse_vertical_iterator = grid_iterator<const grid_range, grid_iterator_direction::up>;

        template<typename, typename, unsigned> friend struct grid_range;
        template<typename, grid_iterator_direction> friend struct grid_iterator;

        constexpr grid_range() noexcept = delete;

        template<unsigned L2 = L, std::enable_if_t<(L2 > 1), bool> = { }>
        constexpr grid_range(const grid_range<R, T, L2>& other) noexcept
            : r(other.r), pos(other.pos), dim(other.dim) { }

        template<unsigned L2 = L, std::enable_if_t<(L2 <= 1), bool> = { }>
        constexpr grid_range(const grid_range<R, T, L2>& other) noexcept
            : r(&other.r == reinterpret_cast<R*>(&other) ? reinterpret_cast<R&>(*this) : other.r)
            , pos(other.pos), dim(other.dim) { }

        template<unsigned L2 = L, std::enable_if_t<(L2 > 1), bool> = { }>
        constexpr grid_range(grid_range<R, T, L2>&& other) noexcept
            : r(other.r), pos(other.pos), dim(other.dim) { }

        template<unsigned L2 = L, std::enable_if_t<(L2 <= 1), bool> = { }>
        constexpr grid_range(grid_range<R, T, L2>&& other) noexcept
            : r(&other.r == reinterpret_cast<R*>(&other) ? reinterpret_cast<R&>(*this) : other.r)
            , pos(other.pos), dim(other.dim) { }

        constexpr grid_range& operator=(const grid_range& other) noexcept { new (this) grid_range { other }; }
        constexpr grid_range& operator=(grid_range&& other) noexcept { new (this) grid_range { other }; }

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

        constexpr const auto& nowrap(vector2i p) const noexcept { return get(p); }
        constexpr auto& nowrap(vector2i p) noexcept { return remove_const(get(p)); }
        constexpr const auto& nowrap(std::ptrdiff_t x, std::ptrdiff_t y) const noexcept { return nowrap({ x, y }); }
        constexpr auto& nowrap(std::ptrdiff_t x, std::ptrdiff_t y) noexcept { return nowrap({ x, y }); }

        template<typename R2, unsigned L2> friend constexpr bool operator==(const grid_range& lhs, const grid_range<R2, T, L2>& rhs) noexcept
        {
            return &lhs(0, 0) == &rhs(0, 0) and &lhs(lhs.size() - vector2i { 1, 1 }) == &rhs(rhs.size() - vector2i { 1, 1 });
        }
        template<typename R2, unsigned L2> friend constexpr bool operator!=(const grid_range& lhs, const grid_range<R2, T, L2>& rhs) noexcept { return not (lhs == rhs); }

        constexpr grid_range& fill(const auto& fill)
        {
            for (auto&& i : *this) i = fill;
            return *this;
        }

        constexpr grid_range& fill_nowrap(const auto& fill)
        {
            for (std::ptrdiff_t y = 0; y < height(); ++y)
            {
                auto left = &remove_const(get({ 0,y })), right = &remove_const(get({ width(), y }));
                std::fill(std::min(left, right), std::max(left, right), fill);
            }
            return *this;
        }

        constexpr grid_range& assign(const auto& copy)
        {
            auto size = vector2i::min(this->size(), copy.size());
            vector2i p { 0, 0 };
            for (p.y() = 0; p.y() < size.y(); ++p.y())
                for (p.x() = 0; p.x() < size.x(); ++p.x())
                    remove_const(get_maybe_wrap(p)) = copy.get_maybe_wrap(p);
            return *this;
        }

        constexpr grid_range& assign_nowrap(const auto& copy)
        {
            auto size = vector2i::min(this->size(), copy.size());
            vector2i p { 0, 0 };
            for (p.y() = 0; p.y() < size.y(); ++p.y())
                for (p.x() = 0; p.x() < size.x(); ++p.x())
                    nowrap(p) = copy.nowrap(p);
            return *this;
        }

        template<typename F> constexpr grid_range& apply(F&& f) { for (auto i = begin(); i != end(); ++i) f(*i); return *this; }
        template<typename F> constexpr grid_range& apply_nowrap(F&& f) { for(auto i = begin(); i != end(); ++i) f(nowrap(i.position())); return *this; }
        template<typename F> constexpr grid_range& apply_pos(F&& f) { for (auto i = begin(); i != end(); ++i) f(i.position(), *i); return *this; }
        template<typename F> constexpr grid_range& apply_pos_nowrap(F&& f) { for (auto i = begin(); i != end(); ++i) f(i.position(), nowrap(i.position())); return *this; }

        constexpr auto begin() noexcept   { return iterator { *this, { 0, 0 } }; }
        constexpr auto vbegin() noexcept  { return vertical_iterator { *this, { 0, 0 } }; }
        constexpr auto rbegin() noexcept  { return reverse_iterator { *this, size() - vector2i { 1, 1 } }; }
        constexpr auto rvbegin() noexcept { return reverse_vertical_iterator { *this, size() - vector2i { 1, 1 } }; }
        constexpr auto cbegin() const noexcept   { return const_iterator { *this, { 0, 0 } }; }
        constexpr auto cvbegin() const noexcept  { return const_vertical_iterator { *this, { 0, 0 } }; }
        constexpr auto crbegin() const noexcept  { return const_reverse_iterator { *this, size() - vector2i { 1, 1 } }; }
        constexpr auto crvbegin() const noexcept { return const_reverse_vertical_iterator { *this, size() - vector2i { 1, 1 } }; }
        constexpr auto end() const noexcept { return invalid_grid_iterator { }; }

        [[gnu::const]] constexpr const auto& position() const noexcept { return pos; }
        [[gnu::const]] constexpr auto size() const noexcept { return std::abs(dim); }
        [[gnu::const]] constexpr auto width() const noexcept { return size().x(); }
        [[gnu::const]] constexpr auto height() const noexcept { return size().y(); }

        constexpr auto& grid() const noexcept
        {
            if constexpr (L <= 1) return r;
            else return r.grid();
        }

        constexpr vector2i abs_pos(vector2i p = { 0, 0 }) const noexcept
        {
            if constexpr (L == 0) return p;
            else return r.abs_pos(pos + p.copysign(dim));
        }

        constexpr vector2i abs_pos_wrap(vector2i p = { 0, 0 }) const noexcept
        {
            if constexpr (L == 0) return p.wrap({ 0, 0 }, size());
            else return r.abs_pos_wrap(pos + p.wrap({ 0, 0 }, size()).copysign(dim));
        }

        constexpr vector2i abs_pos_maybe_wrap(vector2i p = { 0, 0 }) const noexcept
        {
            if constexpr (L == 0) return p;
            else return r.abs_pos_wrap(pos + p.copysign(dim));
        }

    protected:
        template<typename R2>
        constexpr grid_range(R2&& range, const vector2i& position, const vector2i& dimensions) noexcept
            : r(std::forward<R2>(range)), pos(position), dim(dimensions) { }

        template<unsigned level = L + 1, typename Self>
        constexpr auto make_range(Self&& self, vector2i position, vector2i dimensions) const noexcept
        {
            auto dim_sign = dim.sign();
            dimensions[0] *= dim_sign[0]; dimensions[1] *= dim_sign[1];
            return grid_range<std::remove_reference_t<Self>, T, level> { std::forward<Self>(self), position, dimensions };
        }

        constexpr T& remove_const(const T& t) const noexcept { return const_cast<T&>(t); }

        constexpr const T& get_maybe_wrap(vector2i p) const { return grid().base_get(abs_pos_maybe_wrap(p)); }
        constexpr const T& get_wrap(vector2i p) const { return grid().base_get(abs_pos_wrap(p)); }
        constexpr const T& get(vector2i p) const { return grid().base_get(abs_pos(p)); }

        std::conditional_t<(L < 2), R&, R> r;
        const vector2i pos, dim;
    };

#   define COMMON_FUNCTIONS \
    template<typename, typename, unsigned> friend struct grid_range;                                                                                  \
    constexpr auto range(vector2i position, vector2i dimensions) const noexcept { return this->template make_range<1>(*this, position, dimensions); } \
    constexpr auto range(vector2i position, vector2i dimensions) noexcept { return this->template make_range<1>(*this, position, dimensions); }       \
    constexpr auto range_abs(const vector2i& topleft, const vector2i& bottomright) const noexcept { return range(topleft, bottomright - topleft); }   \
    constexpr auto range_abs(const vector2i& topleft, const vector2i& bottomright) noexcept { return range(topleft, bottomright - topleft); }

    template<typename T>
    struct grid : public grid_range<grid<T>, T, 0>
    {
        constexpr grid(vector2i size, T* const data) : grid_range<grid<T>, T, 0>(*this, { 0, 0 }, size), ptr(data) { }
        constexpr grid(std::size_t w, std::size_t h, T* const data) : grid(vector2i { w, h }, data) { }
        COMMON_FUNCTIONS

    protected:
        constexpr const T& base_get(vector2i p) const { return *(ptr + p[0] + this->dim[0] * p[1]); }

        T* const ptr;
    };

    template<typename T>
    struct grid_container : public grid_range<grid_container<T>, T, 0>
    {
        grid_container(vector2i size, std::pmr::memory_resource* memres = std::pmr::get_default_resource())
            : grid_range<grid_container<T>, T, 0>(*this, { 0, 0 }, size), data(size.x()* size.y(), std::pmr::polymorphic_allocator<T> { memres }) { }
        grid_container(std::size_t w, std::size_t h, std::pmr::memory_resource* memres = std::pmr::get_default_resource())
            : grid_container(vector2i { w,h }, memres) { }
        COMMON_FUNCTIONS

    protected:
        constexpr const T& base_get(vector2i p) const { return *(data.data() + p[0] + this->dim[0] * p[1]); }

        std::pmr::vector<T> data;
    };

    template<typename T, std::size_t w, std::size_t h>
    struct fixed_grid : public grid_range<fixed_grid<T, w, h>, T, 0>
    {
        constexpr fixed_grid() : grid_range<fixed_grid<T, w, h>, T, 0>(*this, { 0, 0 }, { w, h }) { }
        COMMON_FUNCTIONS

    protected:
        constexpr const T& base_get(vector2i p) const { return *(array.data() + p[0] + this->dim[0] * p[1]); }

        std::array<T, w * h> array;
    };
#   undef COMMON_FUNCTIONS
}
