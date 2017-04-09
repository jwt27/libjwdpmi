/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/video/pixel.h>
#include <jw/vector2.h>
#include <jw/matrix.h>

namespace jw
{
    namespace video
    {
        template<typename I>
        struct image_range
        {
        #if defined(__MMX__) && !defined(__SSE__)
            using T = std::int16_t;
        #else
            using T = float;
        #endif
            static constexpr std::size_t vs = 4;
            using V [[gnu::vector_size(vs * sizeof(T))]] = T;
            union alignas(0x10) vector
            {
                V v;
                std::array<T, vs> t;
                constexpr vector(T a, T b, T c, T d) noexcept : t({ a, b, c, d }) { }
                constexpr vector(V a) noexcept : v(a) { }
                constexpr vector() noexcept : t() { };
            };

            struct ref
            {
                constexpr ref(auto* matrix, auto* image, vector2i pos) : m(matrix), i(image), p(pos) { }
                operator float() const { return to_float((*m)(p.x / vs, p.y).t[p.x % vs]); }
                ref& operator=(float value) { (*m)(p.x / vs, p.y).t[p.x % vs] = from_float(value); return *this; }

            private:
                matrix_container<vector>* m;
                I* i;
                vector2i p;

                static constexpr float to_float(T v) { if (std::is_floating_point<T>::value) return v; return v / static_cast<float>(std::numeric_limits<T>::max()); }
                static constexpr float from_float(T v) { if (std::is_floating_point<T>::value) return v; return v * static_cast<float>(std::numeric_limits<T>::max()); }
            };

            constexpr image_range(I* image, const vector2i& position, const vector2i& dimensions) noexcept 
                : i(image), pos(position), dim(dimensions) { }

            constexpr image_range() noexcept = delete;
            constexpr image_range(const image_range&) noexcept = default;
            constexpr image_range(image_range&&) noexcept = default;
            constexpr image_range& operator=(const image_range&) noexcept = delete;
            constexpr image_range& operator=(image_range&&) noexcept = default;

            constexpr auto range(const vector2i& position, const vector2i& dimensions) const noexcept
            {
                auto new_pos = pos + vector2i::max_abs(position, vector2i { 0,0 });
                auto new_dim = vector2i::min(dimensions, dim - new_pos).copysign(dimensions);
                return image_range { i, pos + new_pos, new_dim };
            }

            constexpr auto range_abs(const vector2i& topleft, const vector2i& bottomright) const noexcept { return range(topleft, bottomright - topleft); }

            auto r(vector2i p) noexcept { return ref { &i->rm, i, p }; }
            auto g(vector2i p) noexcept { return ref { &i->gm, i, p }; }
            auto b(vector2i p) noexcept { return ref { &i->bm, i, p }; }
            auto a(vector2i p) noexcept { return ref { &i->am, i, p }; }

            auto r(std::ptrdiff_t x, std::ptrdiff_t y) noexcept { return r(vector2i { x, y }); }
            auto g(std::ptrdiff_t x, std::ptrdiff_t y) noexcept { return g(vector2i { x, y }); }
            auto b(std::ptrdiff_t x, std::ptrdiff_t y) noexcept { return b(vector2i { x, y }); }
            auto a(std::ptrdiff_t x, std::ptrdiff_t y) noexcept { return a(vector2i { x, y }); }

            auto r(vector2i p) const noexcept { return ref { &i->rm, i, p }; }
            auto g(vector2i p) const noexcept { return ref { &i->gm, i, p }; }
            auto b(vector2i p) const noexcept { return ref { &i->bm, i, p }; }
            auto a(vector2i p) const noexcept { return ref { &i->am, i, p }; }

            auto r(std::ptrdiff_t x, std::ptrdiff_t y) const noexcept { return r(vector2i { x, y }); }
            auto g(std::ptrdiff_t x, std::ptrdiff_t y) const noexcept { return g(vector2i { x, y }); }
            auto b(std::ptrdiff_t x, std::ptrdiff_t y) const noexcept { return b(vector2i { x, y }); }
            auto a(std::ptrdiff_t x, std::ptrdiff_t y) const noexcept { return a(vector2i { x, y }); }

            // no bounds checking
            constexpr auto operator()(vector2i p) noexcept { return get(p); }
            constexpr const auto operator()(vector2i p) const noexcept { return get(p); }
            constexpr const auto operator()(std::ptrdiff_t x, std::ptrdiff_t y) const noexcept { return (*this)({ x, y }); }
            constexpr auto operator()(std::ptrdiff_t x, std::ptrdiff_t y) noexcept { return (*this)({ x, y }); }

            friend constexpr bool operator==(const image_range& lhs, const image_range& rhs) noexcept { return lhs.m.data() == rhs.m.data() && lhs.pos == rhs.pos && lhs.dim == rhs.dim; }
            friend constexpr bool operator!=(const image_range& lhs, const image_range& rhs) noexcept { return !(lhs == rhs); }

            constexpr image_range& fill(const auto& fill) noexcept
            {
                return *this;
            }

            constexpr image_range& assign(const auto& copy) noexcept
            {
                return *this;
            }

            constexpr image_range& blend(const auto& copy) noexcept
            {
                return *this;
            }

            constexpr auto position() const noexcept { return pos; }
            constexpr auto size() const noexcept { return dim; }
            constexpr auto width() const noexcept { return size().x; }
            constexpr auto height() const noexcept { return size().y; }

        protected:
            constexpr pxf get(vector2i p) noexcept { return pxf { r(pos + p), g(pos + p), b(pos + p), a(pos + p) }; }
            constexpr pxf get(vector2i p) const noexcept { return pxf { r(pos + p), g(pos + p), b(pos + p), a(pos + p) }; }

            I* i;
            vector2i pos, dim;
        };

        struct image : public image_range<image>
        {
            using R = image_range<image>;
            matrix_container<vector> rm, gm, bm, am;

            image(vector2i size) : image(size.x, size.y) { }
            image(std::size_t w, std::size_t h) : R(this, { 0, 0 }, { w, h }),
                rm(w / vs + 1, h), gm(w / vs + 1, h), bm(w / vs + 1, h), am(w / vs + 1, h) { }
        };
    }
}
