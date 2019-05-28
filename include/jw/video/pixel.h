/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <type_traits>
#include <vector>
#include <mmintrin.h>
#include <xmmintrin.h>
#include <jw/common.h>
#include <jw/math.h>

namespace jw
{
    namespace video
    {
        namespace bios_colors
        {
            enum color : byte
            {
                black, blue, green, cyan, red, magenta, brown, light_gray,
                dark_gray, light_blue, light_green, light_cyan, light_red, pink, yellow, white
            };
        }

        union [[gnu::packed]] text_attr
        {
            struct [[gnu::packed]]
            {
                unsigned foreground : 4;
                unsigned background : 3;
                bool blink : 1;
            };
            std::uint8_t raw_value;

            constexpr text_attr() noexcept : text_attr(7, 0, false) { }
            constexpr text_attr(byte fcol, byte bcol, bool _blink) noexcept : foreground(fcol), background(bcol), blink(_blink) { }
        };

        union alignas(2) [[gnu::packed]] text_char
        {
            struct [[gnu::packed]] char_with_attr
            {
                char character;
                text_attr attr;

                constexpr char_with_attr(char c, byte fcol, byte bcol, bool _blink) noexcept : character(c), attr(fcol, bcol, _blink) { }
                constexpr char_with_attr(char c, text_attr a) noexcept : character(c), attr(a) { }
                constexpr char_with_attr(char c) noexcept : character(c), attr() { }
            } value;
            std::uint16_t raw_value;

            constexpr text_char() noexcept : text_char(' ') { }
            constexpr text_char(char c, text_attr a) noexcept : value(c, a) { }
            constexpr text_char(char c, byte fcol = 7, byte bcol = 0, bool _blink = false) noexcept : text_char(c, { fcol, bcol, _blink }) { }
            constexpr explicit text_char(std::uint16_t v) noexcept : raw_value(v) { }
            constexpr explicit operator std::uint16_t() const noexcept{ return raw_value; }
            constexpr text_char& operator=(char c) noexcept { value.character = c; return *this; }
            constexpr operator char() const noexcept{ return value.character; }
        };
        static_assert(sizeof(text_char) == 2 && alignof(text_char) == 2, "text_char has incorrect size or alignment.");

        struct [[gnu::packed]] px { };

#       ifdef __SSE__
#           define PIXEL_FUNCTION [[gnu::hot, gnu::sseregparm, gnu::always_inline]]
#       else
#           define PIXEL_FUNCTION [[gnu::hot, gnu::always_inline]]
#       endif
#       ifdef __MMX__
#           ifdef __SSE__
#               define MMX_FUNCTION [[gnu::hot, gnu::sseregparm, gnu::noinline]]
#           else
#               define MMX_FUNCTION [[gnu::hot, gnu::noinline]]
#           endif
#       else
#           define MMX_FUNCTION PIXEL_FUNCTION
#       endif

        template<typename P>
        struct alignas(P) [[gnu::packed, gnu::may_alias]] pixel : public P, public px
        {
            template<typename> friend struct pixel;

            template<std::size_t N, typename VT>
            using V [[gnu::vector_size(N * sizeof(VT)), gnu::may_alias]] = VT;

            static constexpr auto cast_PT(auto v) { return static_cast<typename P::T>(v); }

            constexpr pixel() noexcept = default;
            template<typename Q = pixel, std::enable_if_t<Q::has_alpha(), bool> = { }>
            constexpr pixel(auto cr, auto cg, auto cb, auto ca) noexcept : P { cast_PT(cb), cast_PT(cg), cast_PT(cr), cast_PT(ca) } { }
            template<typename Q = pixel, std::enable_if_t<Q::has_alpha(), bool> = { }>
            constexpr pixel(auto cr, auto cg, auto cb) noexcept : pixel { cr, cg, cb, P::ax } { }
            template<typename Q = pixel, std::enable_if_t<not Q::has_alpha(), bool> = { }>
            constexpr pixel(auto cr, auto cg, auto cb, auto) noexcept : pixel { cr, cg, cb } { }
            template<typename Q = pixel, std::enable_if_t<not Q::has_alpha(), bool> = { }>
            constexpr pixel(auto cr, auto cg, auto cb) noexcept : P { cast_PT(cb), cast_PT(cg), cast_PT(cr) } { }

            constexpr pixel(const pixel& p) noexcept = default;
            constexpr pixel(pixel&& p) noexcept = default;
            constexpr pixel& operator=(const pixel&) noexcept = default;
            constexpr pixel& operator=(pixel&&) noexcept = default;

            template <typename U> constexpr operator pixel<U>() const noexcept { return cast_to<U>(); }

            static constexpr bool has_alpha() { return P::ax > 0; }

            template<typename U>
            PIXEL_FUNCTION constexpr pixel& blend(const pixel<U>& other)
            {
                if constexpr (not pixel<U>::has_alpha())
                {
                    *this = other.template cast_to<P>();
                }
                else if constexpr (sse and (std::is_floating_point_v<typename P::T> or std::is_floating_point_v<typename U::T>))
                {
                    auto do_blend = [this, &other]() PIXEL_FUNCTION { *this = m128(m128_blend<U>(m128(), other.m128())); };
                    if constexpr (std::is_integral_v<typename P::T> or std::is_integral_v<typename U::T>)
                         [&do_blend]() MMX_FUNCTION { do_blend(); }();
                    else [&do_blend]() PIXEL_FUNCTION { do_blend(); }();
                    if constexpr (std::is_integral_v<typename U::T>) _mm_empty();
                }
                else if constexpr (mmx and std::is_integral_v<typename P::T> and std::is_integral_v<typename U::T>)
                {
                    [this, &other]() MMX_FUNCTION { *this = m64(m64_blend<U>(m64(), other.m64())); }();
                }
                else
                {
                    using VT = std::conditional_t<std::is_floating_point_v<typename P::T> or std::is_floating_point_v<typename U::T>, float, std::uint32_t>;
                    V<4, VT> src = other.template vector<VT>();
                    V<4, VT> dst = vector<VT>();
                    *this = vector<VT>(vector_blend<U, VT>(dst, src));
                }
                return *this;
            }

            template<typename U>
            PIXEL_FUNCTION constexpr pixel& blend_straight(const pixel<U>& other)
            {
                if constexpr (not pixel<U>::has_alpha())
                {
                    *this = other.template cast_to<P>();
                }
                else if constexpr (sse and (std::is_floating_point_v<typename P::T> or std::is_floating_point_v<typename U::T>))
                {
                    auto do_blend = [this, &other]() PIXEL_FUNCTION {*this = m128(m128_blend<U>(m128_premul(m128()), m128_premul(other.m128()))); };
                    if constexpr (std::is_integral_v<typename P::T> or std::is_integral_v<typename U::T>)
                         [&do_blend]() MMX_FUNCTION { do_blend(); }();
                    else [&do_blend]() PIXEL_FUNCTION { do_blend(); }();
                    if constexpr (std::is_integral_v<typename U::T>) _mm_empty();
                }
                else if constexpr (mmx and std::is_integral_v<typename P::T> and std::is_integral_v<typename U::T>)
                {
                    [this, &other]() MMX_FUNCTION { *this = m64(m64_blend<U>(m64_premul(m64()), m64_premul(other.m64()))); }();
                }
                else
                {
                    using VT = std::conditional_t<std::is_floating_point_v<typename P::T> or std::is_floating_point_v<typename U::T>, float, std::uint32_t>;
                    V<4, VT> src = vector_premul<VT>(other.template vector<VT>());
                    V<4, VT> dst = vector_premul<VT>(vector<VT>());
                    *this = vector<VT>(vector_blend<U, VT>(dst, src));
                }
                return *this;
            }

            PIXEL_FUNCTION constexpr pixel& premultiply_alpha()
            {
                if constexpr (not has_alpha()) return *this;
                if constexpr (sse and std::is_floating_point_v<typename P::T>) *this = m128(m128_premul(m128()));
                else if constexpr (mmx and not std::is_floating_point_v<typename P::T>) *this = m64(m64_premul(m64()));
                else
                {
                    using VT = std::conditional_t<std::is_floating_point_v<typename P::T>, float, std::uint32_t>;
                    *this = vector<VT>(vector_premul<VT>(vector<VT>()));
                }
                return *this;
            }

        private:
            template <typename U>
            PIXEL_FUNCTION constexpr pixel<U> cast_to() const
            {
                constexpr bool not_constexpr = true;// not is_constexpr(this->b);
                if constexpr (not_constexpr and sse and (std::is_floating_point_v<typename P::T> or std::is_floating_point_v<typename U::T>))
                {
                    auto do_cast = [this]() PIXEL_FUNCTION { return pixel<U>::m128(m128_cast_to<U>(m128())); };
                    if constexpr (std::is_integral_v<typename P::T> or std::is_integral_v<typename U::T>)
                        return [&do_cast]() MMX_FUNCTION { auto r = do_cast(); _mm_empty(); return r; }();
                    else return [&do_cast]() PIXEL_FUNCTION { return do_cast(); }();
                }
                else if constexpr (not_constexpr and mmx and (sse or (std::is_integral_v<typename P::T> and std::is_integral_v<typename U::T>)))
                {
                    return [this]() MMX_FUNCTION { return pixel<U>::m64(m64_cast_to<U>(m64())); }();
                }
                else
                {
                    using VT = std::conditional_t<std::is_floating_point_v<typename P::T> or std::is_floating_point_v<typename U::T>, float, std::uint32_t>;
                    return pixel<U>::template vector<VT>(vector_cast_to<U, VT>(vector<VT>()));
                }
            }

            PIXEL_FUNCTION static constexpr pixel m64(__m64 value) noexcept // V4HI
            {
                static_assert(not std::is_floating_point_v<typename P::T>);
                auto v = _mm_packs_pu16(value, _mm_setzero_si64());
                if constexpr (byte_aligned())
                {
                    auto v2 = _mm_cvtsi64_si32(v);
                    pixel result { *reinterpret_cast<pixel*>(&v2) };
                    _mm_empty();
                    return result;
                }
                else
                {
                    auto v2 = reinterpret_cast<V<8, byte>&>(v);
                    pixel result { v2[2], v2[1], v2[0], v2[3] };
                    _mm_empty();
                    return result;
                }
            }

            PIXEL_FUNCTION constexpr __m64 m64() const noexcept    // V4HI
            {
                static_assert(not std::is_floating_point_v<typename P::T>);
                __m64 v;
                if constexpr (byte_aligned()) v = _mm_cvtsi32_si64(*reinterpret_cast<const int*>(this));
                else if constexpr (has_alpha()) v = _mm_setr_pi8(this->b, this->g, this->r, this->a, 0, 0, 0, 0);
                else v = _mm_setr_pi8(this->b, this->g, this->r, 0, 0, 0, 0, 0);
                auto r = _mm_unpacklo_pi8(v, _mm_setzero_si64());
                return r;
            }

            PIXEL_FUNCTION static constexpr pixel m128(__m128 value) noexcept  // V4SF
            {
                if constexpr (std::is_floating_point_v<typename P::T>) return *reinterpret_cast<pixel*>(&value);
                else return m64(_mm_cvtps_pi16(value));
            }

            PIXEL_FUNCTION constexpr __m128 m128() const noexcept  // V4SF
            {
                if constexpr (std::is_floating_point_v<typename P::T>) return *reinterpret_cast<const __m128*>(this);
                else return _mm_cvtpu16_ps(m64());
            }

            template<typename VT = std::uint16_t>
            PIXEL_FUNCTION static constexpr pixel vector(V<4, VT> src) noexcept
            {
                if constexpr ((std::is_same_v<VT, float> and std::is_same_v<typename P::T, float>) or (sizeof(VT) == 1 and byte_aligned()))
                {
                    return *reinterpret_cast<pixel*>(&src);
                }
                return pixel { src[2], src[1], src[0], src[3] };
            }

            template<typename VT = std::uint16_t>
            PIXEL_FUNCTION constexpr V<4, VT> vector() const noexcept
            {
                V<4, VT> src;
                if constexpr ((std::is_same_v<VT, float> and std::is_same_v<typename P::T, float>) or (sizeof(VT) == 1 and byte_aligned()))
                {
                    src = *reinterpret_cast<const V<4, VT>*>(this);
                    if constexpr (has_alpha()) src = V<4, VT> { src[0], src[1], src[2], 1 };
                }
                else if constexpr (has_alpha()) src = V<4, VT> { static_cast<VT>(this->b), static_cast<VT>(this->g), static_cast<VT>(this->r), static_cast<VT>(this->a), };
                else src = V<4, VT> { static_cast<VT>(this->b), static_cast<VT>(this->g), static_cast<VT>(this->r), 1 };
                return src;
            }

            template <typename U>
            PIXEL_FUNCTION static constexpr __m128 m128_cast_to(__m128 src) noexcept
            {
                constexpr __m128 cast = reinterpret_cast<__m128>(pixel<U>::template vector_max<float>(P::ax) * (1.0f / vector_max<float>(U::ax or 1.0f)));
                src = _mm_mul_ps(src, cast);
                if constexpr (pixel<U>::has_alpha() and not has_alpha()) src = _mm_setr_ps(src[0], src[1], src[2], static_cast<float>(U::ax));
                return src;
            }

            template <typename U>
            PIXEL_FUNCTION static constexpr __m64 m64_cast_to(__m64 src) noexcept
            {
                constexpr auto mullo = reinterpret_cast<__m64>(pixel<U>::template vector_max<std::uint16_t>(P::ax));
                constexpr auto mulhi = reinterpret_cast<__m64>(vector_max_reciprocal<17, std::uint16_t, 15>(U::ax | 1));
                auto vector_max_contains = [](std::uint16_t value)
                {
                    auto v = vector_max<std::uint16_t>(U::ax | 1);
                    for (auto i = 0; i < 4; ++i) if (v[i] == value) return true;
                    return false;
                };

                src = _mm_mullo_pi16(src, mullo);
                auto dst = _mm_mulhi_pi16(src, mulhi);
                dst = _mm_srli_pi16(_mm_adds_pu8(dst, _mm_set1_pi16(1)), 1);

                if constexpr (vector_max_contains(1))
                {
                    constexpr auto is1 = reinterpret_cast<__m64>(vector_max<std::uint16_t>(U::ax) == 1);
                    auto v1 = _mm_and_si64(src, is1);
                    dst = _mm_or_si64(_mm_andnot_si64(is1, dst), v1);
                }
                if constexpr (vector_max_contains(3))
                {
                    constexpr auto mulhi3 = reinterpret_cast<__m64>(vector_max_reciprocal<16, std::uint16_t, 15>(U::ax | 1));
                    constexpr auto is3 = reinterpret_cast<__m64>(vector_max<std::uint16_t>() == 3);
                    auto v3 = _mm_mulhi_pi16(_mm_and_si64(src, is3), mulhi3);
                    dst = _mm_or_si64(_mm_andnot_si64(is3, dst), v3);
                }
                if constexpr (pixel<U>::has_alpha() and not has_alpha())
                {
                    if constexpr (sse) dst = _mm_insert_pi16(dst, U::ax, 3);
                    else
                    {
                        dst = _mm_and_si64(dst, _mm_setr_pi16(0xffff, 0xffff, 0xffff, 0));
                        dst = _mm_or_si64(dst, _mm_setr_pi16(0, 0, 0, U::ax));
                    }
                }
                return dst;
            }

            template <typename U, typename VT>
            PIXEL_FUNCTION static constexpr V<4, VT> vector_cast_to(V<4, VT> src) noexcept
            {
                if constexpr (std::is_floating_point_v<VT>)
                {
                    src *= pixel<U>::template vector_max<VT>(P::ax) * (1.0f / vector_max<VT>(U::ax or 1.0f));
                }
                else
                {
                    constexpr auto rbits = (sizeof(VT) - 1) * 8;
                    src *= pixel<U>::template vector_max<VT>(P::ax | 1);
                    src *= vector_max_reciprocal<rbits, VT>(U::ax | 1);
                    src += 1 << (rbits - 1);
                    src >>= rbits;
                }
                if constexpr (has_alpha()) return src;
                else return V<4, VT> { src[0], src[1], src[2], static_cast<VT>(U::ax) };
            }

            PIXEL_FUNCTION static constexpr __m128 m128_premul(__m128 src) noexcept
            {
                if constexpr (not has_alpha()) return src;
                constexpr auto ax = reinterpret_cast<__m128>(1.0f / V<4, float> { P::ax, P::ax, P::ax, 1 });
                auto srca = _mm_setr_ps(src[3], src[3], src[3], 1);
                src = _mm_mul_ps(src, srca);
                src = _mm_mul_ps(src, ax);
                return src;
            }

            PIXEL_FUNCTION static constexpr __m64 m64_premul(__m64 src) noexcept
            {
                if constexpr (not has_alpha()) return src;
                constexpr auto mask = shuffle_mask(3, 3, 3, 3);
                auto scalar_a = reinterpret_cast<V<4, std::uint16_t>>(src)[3];
                auto a = _mm_shuffle_pi16(src, mask);
                src = _mm_mullo_pi16(src, a);
                if constexpr (P::ax == 3)
                {
                    constexpr auto ax = vector_reciprocal<16, std::uint16_t, 15>(P::ax);
                    src = _mm_mulhi_pi16(src, reinterpret_cast<__m64>(ax));
                }
                else if constexpr (P::ax > 3)
                {
                    constexpr auto ax = vector_reciprocal<17, std::uint16_t, 15>(P::ax);
                    src = _mm_mulhi_pi16(src, reinterpret_cast<__m64>(ax));
                    src = _mm_srli_pi16(_mm_adds_pu8(src, _mm_set1_pi16(1)), 1);
                }
                src = _mm_insert_pi16(src, scalar_a, 3);
                return src;
            }

            template <typename VT>
            PIXEL_FUNCTION static constexpr V<4, VT> vector_premul(V<4, VT> src) noexcept
            {
                if constexpr (not has_alpha()) return src;
                auto a = V<4, VT> { src[3], src[3], src[3], 1 };
                if constexpr (std::is_floating_point_v<VT>)
                {
                    constexpr auto ax = 1.0f / V<4, float> { P::ax, P::ax, P::ax, 1 };
                    src *= a * ax;
                }
                else
                {
                    constexpr auto rbits = (sizeof(VT) - 1) * 8;
                    constexpr auto ax = vector_reciprocal<rbits, VT>(P::ax, P::ax, P::ax, 1);
                    src *= a;
                    src *= ax;
                    src += 1 << (rbits - 1);
                    src >>= rbits;
                }
                return src;
            }

            template <typename U>
            PIXEL_FUNCTION constexpr __m128 m128_blend(__m128 dst, __m128 src)
            {
                constexpr auto ax = reinterpret_cast<__m128>(1.0f / V<4, float> { U::ax, U::ax, U::ax, U::ax });
                auto a = _mm_sub_ps(_mm_set1_ps(U::ax), _mm_set1_ps(src[3]));

                if constexpr (not std::is_same_v<P, U>) src = pixel<U>::template m128_cast_to<P>(src);
                dst = _mm_mul_ps(dst, a);
                dst = _mm_mul_ps(dst, ax);
                dst = _mm_add_ps(dst, src);
                return dst;
            }

            template <typename U>
            PIXEL_FUNCTION constexpr __m64 m64_blend(__m64 dst, __m64 src)
            {
                //auto a = _mm_sub_pi16(_mm_set1_pi16(U::ax), _mm_shuffle_pi16(src, shuffle_mask(3, 3, 3, 3)));
                auto a = _mm_set1_pi16(U::ax - reinterpret_cast<V<4, std::uint16_t>>(src)[3]);

                if constexpr (not std::is_same_v<P, U>) src = pixel<U>::template m64_cast_to<P>(src);
                dst = _mm_mullo_pi16(dst, a);
                if constexpr (U::ax == 3)
                {
                    constexpr auto ax = vector_reciprocal<16, std::uint16_t, 15>(U::ax);
                    dst = _mm_mulhi_pi16(dst, reinterpret_cast<__m64>(ax));
                }
                else if constexpr (U::ax != 1)
                {
                    constexpr auto ax = vector_reciprocal<17, std::uint16_t, 15>(U::ax);
                    dst = _mm_mulhi_pi16(dst, reinterpret_cast<__m64>(ax));
                    dst = _mm_srli_pi16(_mm_adds_pu8(dst, _mm_set1_pi16(1)), 1);
                }
                dst = _mm_adds_pu8(dst, src);
                return dst;
            }

            template <typename U, typename VT>
            PIXEL_FUNCTION static constexpr V<4, VT> vector_blend(V<4, VT> dst, V<4, VT> src) noexcept
            {
                if constexpr (not std::is_same_v<P, U>) src = pixel<U>::template vector_cast_to<P, VT>(src);
                if constexpr (std::is_floating_point_v<VT>)
                {
                    constexpr auto ax = 1.0f / U::ax;
                    dst *= static_cast<VT>(U::ax - src[3]) * ax;
                    dst += src;
                }
                else
                {
                    constexpr auto rbits = (sizeof(VT) - 1) * 8;
                    constexpr auto ax = vector_reciprocal<rbits, VT>(U::ax);
                    dst *= static_cast<VT>(U::ax - src[3]);
                    dst *= ax;
                    dst += 1 << (rbits - 1);
                    dst >>= rbits;
                    dst += src;
                }
                return dst;
            }

            template<std::size_t bits, typename VT = std::uint16_t, std::size_t maxbits = bits>
            static constexpr auto vector_reciprocal(VT v0, VT v1, VT v2, VT v3) noexcept
            {
                auto r = [](VT v) -> VT { return std::min(((1ul << bits) + v - 1) / v, (1ul << maxbits) - 1); };
                return V<4, VT> { r(v0), r(v1), r(v2), r(v3)};
            }

            template<std::size_t bits, typename VT = std::uint16_t, std::size_t maxbits = bits>
            static constexpr auto vector_reciprocal(VT v0) noexcept
            {
                return vector_reciprocal<bits, VT, maxbits>(v0, v0, v0, v0);
            }

            template<typename VT = float>
            static constexpr auto vector_max(VT noalpha = 1) noexcept
            {
                return V<4, VT> { P::bx, P::gx, P::rx, static_cast<VT>(has_alpha() ? P::ax : noalpha) };
            }

            template<std::size_t bits, typename VT = std::uint16_t, std::size_t maxbits = bits>
            static constexpr auto vector_max_reciprocal(VT noalpha = 1) noexcept
            {
                return vector_reciprocal<bits, VT, maxbits>(P::bx, P::gx, P::rx, static_cast<VT>(has_alpha() ? P::ax : noalpha));
            }

            template<typename T> constexpr bool is_constexpr(T value) { return __builtin_constant_p(value); }
            static constexpr std::uint8_t shuffle_mask(int v0, int v1, int v2, int v3) noexcept { return (v0 & 3) | ((v1 & 3) << 2) | ((v2 & 3) << 4) | ((v3 & 3) << 6); }
            static constexpr bool byte_aligned() noexcept { return P::byte_aligned; }
        };

#       undef MMX_NOINLINE
#       undef PIXEL_FUNCTION

        struct [[gnu::packed]] px8
        {
            byte value { };

            constexpr px8() noexcept = default;
            constexpr px8(byte v) : value(v) { }
            constexpr px8& operator=(byte p) { value = (p == 0 ? value : p); return *this; }
            constexpr px8& operator=(const px8& p) { value = (p.value == 0 ? value : p.value); return *this; }
            constexpr operator byte() { return value; }

            template<typename T> constexpr auto cast(const auto& pal) { const auto& p = pal[value]; return T { p.r, p.g, p.b }; }
        };

        template<std::size_t bbits, std::size_t gbits, std::size_t rbits, std::size_t abits, std::size_t align = 1, bool is_byte_aligned = false>
        struct alignas(align) [[gnu::packed]] bgra
        {
            using T = unsigned;
            T b : bbits;
            T g : gbits;
            T r : rbits;
            T a : abits;

            static constexpr T rx = (1 << rbits) - 1;
            static constexpr T gx = (1 << gbits) - 1;
            static constexpr T bx = (1 << bbits) - 1;
            static constexpr T ax = (1 << abits) - 1;
            static constexpr bool byte_aligned = is_byte_aligned;
        };

        template<std::size_t bbits, std::size_t gbits, std::size_t rbits, std::size_t align = 1, bool is_byte_aligned = false>
        struct alignas(align) [[gnu::packed]] bgr
        {
            using T = unsigned;
            T b : bbits;
            T g : gbits;
            T r : rbits;

            static constexpr T rx = (1 << rbits) - 1;
            static constexpr T gx = (1 << gbits) - 1;
            static constexpr T bx = (1 << bbits) - 1;
            static constexpr T ax = 0;
            static constexpr bool byte_aligned = is_byte_aligned;
        };

        struct alignas(0x10) bgra_ffff
        {
            using T = float;
            T b, g, r, a;

            static constexpr T rx = 1.0f;
            static constexpr T gx = 1.0f;
            static constexpr T bx = 1.0f;
            static constexpr T ax = 1.0f;
            static constexpr bool byte_aligned = false;
        };

        struct alignas(0x10) bgra_fff0
        {
            using T = float;
            T b, g, r;
            unsigned : sizeof(float);

            static constexpr T rx = 1.0f;
            static constexpr T gx = 1.0f;
            static constexpr T bx = 1.0f;
            static constexpr T ax = 0.0f;
            static constexpr bool byte_aligned = false;
        };

        struct [[gnu::packed]] alignas(4) bgra_8880
        {
            using T = std::uint8_t;
            T b, g, r;
            T : 8;

            static constexpr T rx = 255;
            static constexpr T gx = 255;
            static constexpr T bx = 255;
            static constexpr T ax = 0;
            static constexpr bool byte_aligned = true;
        };

        struct [[gnu::packed]] bgra_6668
        {
            using T = unsigned;
            T b : 6, : 2;
            T g : 6, : 2;
            T r : 6, : 2;
            T a : 8;

            constexpr bgra_6668(T vb, T vg, T vr, T va) noexcept : b(vb), g(vg), r(vr), a(va) { }

            static constexpr T rx = 63;
            static constexpr T gx = 63;
            static constexpr T bx = 63;
            static constexpr T ax = 255;
            static constexpr bool byte_aligned = true;
        };

        struct alignas(2) [[gnu::packed]] bgra_5550
        {
            using T = unsigned;
            T b : 5;
            T g : 5;
            T r : 5;
            T : 1;

            static constexpr T rx = 31;
            static constexpr T gx = 31;
            static constexpr T bx = 31;
            static constexpr T ax = 0;
            static constexpr bool byte_aligned = false;
        };

        using bgra_8888 = bgra<8, 8, 8, 8, 4, true>;
        using bgra_5551 = bgra<5, 5, 5, 1, 2, false>;
        using bgra_4444 = bgra<4, 4, 4, 4, 2, false>;
        using bgra_2222 = bgra<2, 2, 2, 2, 1, false>;
        using bgra_2321 = bgra<2, 3, 2, 1, 1, false>;

        using bgr_8880 = bgr<8, 8, 8, 1, true>;
        using bgr_5650 = bgr<5, 6, 5, 2, false>;
        using bgr_2330 = bgr<2, 3, 3, 1, false>;

        using pxf    = pixel<bgra_ffff>;     // floating-point for use with SSE
        using pxfn   = pixel<bgra_fff0>;     // floating-point, no alpha
        using px32a  = pixel<bgra_8888>;     // 24-bit, 8-bit alpha channel
        using px32n  = pixel<bgra_8880>;     // 24-bit, no alpha, 4 bytes wide
        using px24   = pixel<bgr_8880>;      // 24-bit, 3 bytes wide
        using px16   = pixel<bgr_5650>;      // 16-bit, typical 5:6:5 format
        using px16a  = pixel<bgra_5551>;     // 15-bit with 1-bit alpha
        using px16n  = pixel<bgra_5550>;     // 15-bit, no alpha, equal 5:5:5 format
        using px16aa = pixel<bgra_4444>;     // 12-bit, 4-bit alpha, equal 4:4:4 format
        using px8aa  = pixel<bgra_2222>;     // 6-bit 2:2:2, 2-bit alpha
        using px8a   = pixel<bgra_2321>;     // 7-bit 2:3:2, 1-bit alpha
        using px8n   = pixel<bgr_2330>;      // 8-bit 3:3:2, no alpha
        using pxvga  = pixel<bgra_6668>;     // VGA DAC palette format

        static_assert(sizeof(pxf   ) == 16);
        static_assert(sizeof(pxfn  ) == 16);
        static_assert(sizeof(px32a ) ==  4);
        static_assert(sizeof(px32n ) ==  4);
        static_assert(sizeof(px24  ) ==  3);
        static_assert(sizeof(px16  ) ==  2);
        static_assert(sizeof(px16aa) ==  2);
        static_assert(sizeof(px16a ) ==  2);
        static_assert(sizeof(px16n ) ==  2);
        static_assert(sizeof(px8aa ) ==  1);
        static_assert(sizeof(px8a  ) ==  1);
        static_assert(sizeof(px8n  ) ==  1);
        static_assert(sizeof(pxvga ) ==  4);

        inline auto generate_px8n_palette()
        {
            std::vector<px32n> result;
            result.reserve(256);
            for (auto i = 0; i < 256; ++i)
                result.emplace_back(reinterpret_cast<px8n&>(i));
            return result;
        }
    }
}
