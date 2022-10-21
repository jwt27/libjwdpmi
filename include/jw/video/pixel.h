/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <type_traits>
#include <array>
#include <mmintrin.h>
#include <xmmintrin.h>
#include <jw/mmx.h>
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

#       ifdef HAVE__SSE__
#           define PIXEL_FUNCTION [[gnu::hot, gnu::sseregparm, gnu::always_inline]]
#       else
#           define PIXEL_FUNCTION [[gnu::hot, gnu::always_inline]]
#       endif


        template<typename P>
        struct alignas(P) [[gnu::packed, gnu::may_alias]] pixel : public P
        {
            template<typename> friend struct pixel;

            using T = typename P::T;

            static constexpr auto cast_PT(auto v) noexcept { return static_cast<typename P::T>(v); }

            constexpr pixel() noexcept = default;
            template<typename Q = pixel, std::enable_if_t<Q::has_alpha(), bool> = { }>
            constexpr pixel(auto cr, auto cg, auto cb, auto ca) noexcept : P { cast_PT(cb), cast_PT(cg), cast_PT(cr), cast_PT(ca) } { }
            template<typename Q = pixel, std::enable_if_t<Q::has_alpha(), bool> = { }>
            constexpr pixel(auto cr, auto cg, auto cb) noexcept : pixel { cr, cg, cb, P::ax } { }
            template<typename Q = pixel, std::enable_if_t<not Q::has_alpha(), bool> = { }>
            constexpr pixel(auto cr, auto cg, auto cb, auto) noexcept : pixel { cr, cg, cb } { }
            template<typename Q = pixel, std::enable_if_t<not Q::has_alpha(), bool> = { }>
            constexpr pixel(auto cr, auto cg, auto cb) noexcept : P { cast_PT(cb), cast_PT(cg), cast_PT(cr) } { }

            constexpr pixel(const pixel& p) noexcept { assign<simd::none>(p); }
            constexpr pixel(pixel&& p) noexcept { assign<simd::none>(p); }
            constexpr pixel& operator=(const pixel& p) noexcept { return assign<simd::none>(p); };
            constexpr pixel& operator=(pixel&& p) noexcept { return assign<simd::none>(p); };

            template <typename U> constexpr explicit operator pixel<U>() const noexcept { return cast_to<U, simd::none>(); }

            static constexpr bool has_alpha() { return P::ax > 0; }

            template<simd flags, typename U>
            PIXEL_FUNCTION static constexpr pixel convert(const pixel<U>& other)
            {
                return other.template cast_to<P, flags>();
            }

            template<simd flags, typename U>
            PIXEL_FUNCTION constexpr pixel& blend_premultiplied(const pixel<U>& other)
            {
                if constexpr (not pixel<U>::has_alpha()) return *this = other.template cast_to<P>();

                auto do_blend_vector = [this, &other]
                {
                    using VT = std::conditional_t<std::is_floating_point_v<typename P::T> or std::is_floating_point_v<typename U::T>, float, std::int32_t>;
                    *this = vector<VT>(vector_blend_premul<U, VT>(vector<VT>(), other.template vector<VT>()));
                };

                if (std::is_constant_evaluated())
                    do_blend_vector();
                else if constexpr (flags.match(simd::sse) and (std::is_floating_point_v<typename P::T> or std::is_floating_point_v<typename U::T>))
                    *this = m128(m128_blend_premul<U>(m128(), other.m128()));
                else if constexpr (flags.match(simd::mmx) and std::is_integral_v<typename P::T> and std::is_integral_v<typename U::T>)
                    *this = m64(m64_blend_premul<U, flags>(m64(), other.m64()));
                else do_blend_vector();
                return *this;
            }

            template<simd flags, typename U>
            PIXEL_FUNCTION constexpr pixel& blend(const pixel<U>& other)
            {
                if constexpr (not pixel<U>::has_alpha()) return *this = other.template cast_to<P>();

                auto do_blend_vector = [this, &other]
                {
                    using VT = std::conditional_t<std::is_floating_point_v<typename P::T> or std::is_floating_point_v<typename U::T>, float, std::int32_t>;
                    *this = vector<VT>(vector_blend_straight<U, VT>(vector<VT>(), other.template vector<VT>()));
                };

                if (std::is_constant_evaluated())
                    do_blend_vector();
                else if constexpr (flags.match(simd::sse) and (std::is_floating_point_v<typename P::T> or std::is_floating_point_v<typename U::T>))
                    *this = m128(m128_blend_straight<U>(m128(), other.m128()));
                else if constexpr (flags.match(simd::mmx) and std::is_integral_v<typename P::T> and std::is_integral_v<typename U::T>)
                    *this = m64(m64_blend_straight<U, flags>(m64(), other.m64()));
                else do_blend_vector();
                return *this;
            }

            template<simd flags>
            PIXEL_FUNCTION constexpr pixel& premultiply_alpha()
            {
                if constexpr (not has_alpha()) return *this;

                auto do_premul_vector = [this]
                {
                    using VT = std::conditional_t<std::is_floating_point_v<typename P::T>, float, std::int32_t>;
                    *this = vector<VT>(vector_premul<VT>(vector<VT>()));
                };

                if (std::is_constant_evaluated())
                    do_premul_vector();
                if constexpr (flags.match(simd::sse) and std::is_floating_point_v<typename P::T>)
                    *this = m128(m128_premul(m128()));
                else if constexpr (flags.match(simd::mmx) and not std::is_floating_point_v<typename P::T>)
                    *this = m64(m64_premul<flags>(m64()));
                else do_premul_vector();
                return *this;
            }

            template <simd flags, typename U>
            PIXEL_FUNCTION constexpr pixel& assign(const pixel<U>& p) noexcept
            {
                if constexpr (std::is_same_v<P, U>)
                {
                    if constexpr (sizeof(pixel) == 16) *reinterpret_cast<__m128*>(this) = *reinterpret_cast<const __m128*>(&p);
                    else if constexpr (sizeof(pixel) == 8) *reinterpret_cast<__m64*>(this) = *reinterpret_cast<const __m64*>(&p);
                    else std::memcpy(this, &p, sizeof(pixel));
                    return *this;
                }
                else return assign<flags>(p.template cast_to<P, flags>());
            }

        private:
            template <typename U, simd flags>
            PIXEL_FUNCTION constexpr pixel<U> cast_to() const
            {
                auto do_cast_vector = [this]()
                {
                    using VT = std::conditional_t<std::is_floating_point_v<typename P::T> or std::is_floating_point_v<typename U::T>, float, std::int32_t>;
                    return pixel<U>::template vector<VT>(vector_cast_to<U, VT>(vector<VT>()));
                };

                if (std::is_constant_evaluated())
                    return do_cast_vector();
                else if constexpr (flags.match(simd::sse) and (std::is_floating_point_v<typename P::T> or std::is_floating_point_v<typename U::T>))
                    return pixel<U>::m128(m128_cast_to<U>(m128()));
                else if constexpr (flags.match(simd::mmx) and (flags.match(simd::sse) or (std::is_integral_v<typename P::T> and std::is_integral_v<typename U::T>)))
                    return pixel<U>::m64(m64_cast_to<U, flags>(m64()));
                else return do_cast_vector();
            }

            PIXEL_FUNCTION static pixel m64(__m64 value) noexcept // V4HI
            {
                static_assert(not std::is_floating_point_v<typename P::T>);
                const auto v = _mm_cvtsi64_si32(_mm_packs_pu16(value, _mm_setzero_si64()));
                if constexpr (byte_aligned())
                {
                    return *reinterpret_cast<const pixel*>(&v);
                }
                else
                {
                    const auto* const v2 = reinterpret_cast<const std::uint8_t*>(&v);
                    pixel result { v2[2], v2[1], v2[0], v2[3] };
                    return result;
                }
            }

            PIXEL_FUNCTION __m64 m64() const noexcept    // V4HI
            {
                static_assert(not std::is_floating_point_v<typename P::T>);
                __m64 v;
                if constexpr (byte_aligned()) v = _mm_cvtsi32_si64(*reinterpret_cast<const int*>(this));
                else
                {
                    std::uint8_t a[4];
                    a[0] = this->b;
                    a[1] = this->g;
                    a[2] = this->r;
                    if constexpr (has_alpha()) a[3] = this->a;
                    v = _mm_cvtsi32_si64(*reinterpret_cast<const int*>(a));
                }
                return _mm_unpacklo_pi8(v, _mm_setzero_si64());
            }

            PIXEL_FUNCTION static pixel m128(__m128 value) noexcept  // V4SF
            {
                if constexpr (std::is_floating_point_v<typename P::T>) return *reinterpret_cast<pixel*>(&value);
                else return m64(_mm_cvtps_pi16(value));
            }

            PIXEL_FUNCTION __m128 m128() const noexcept  // V4SF
            {
                if constexpr (std::is_floating_point_v<typename P::T>) return *reinterpret_cast<const __m128*>(this);
                else return _mm_cvtpu16_ps(m64());
            }

            template<typename VT = std::uint16_t>
            PIXEL_FUNCTION static constexpr pixel vector(simd_vector<VT, 4> src) noexcept
            {
                if constexpr ((std::is_same_v<VT, float> and std::is_same_v<typename P::T, float>) or (sizeof(VT) == 1 and byte_aligned()))
                    return *reinterpret_cast<pixel*>(&src);
                return pixel { src[2], src[1], src[0], src[3] };
            }

            template<typename VT = std::uint16_t>
            PIXEL_FUNCTION constexpr simd_vector<VT, 4> vector() const noexcept
            {
                if constexpr ((std::is_same_v<VT, float> and std::is_same_v<typename P::T, float>) or (sizeof(VT) == 1 and byte_aligned()))
                    return *reinterpret_cast<const simd_vector<VT, 4>*>(this);
                else if constexpr (has_alpha())
                    return simd_vector<VT, 4> { static_cast<VT>(this->b), static_cast<VT>(this->g), static_cast<VT>(this->r), static_cast<VT>(this->a), };
                else
                    return simd_vector<VT, 4> { static_cast<VT>(this->b), static_cast<VT>(this->g), static_cast<VT>(this->r), 0 };
            }

            template <typename U>
            PIXEL_FUNCTION static __m128 m128_cast_to(__m128 src) noexcept
            {
                constexpr __m128 cast = reinterpret_cast<__m128>(pixel<U>::template vector_max<float>(P::ax) * (1.0f / vector_max<float>(U::ax > 0 ? U::ax : 1.0f)));
                src = _mm_mul_ps(src, cast);
                if constexpr (pixel<U>::has_alpha() and not has_alpha())
                    src[3] = U::ax;
                return src;
            }

            template <typename U, simd flags>
            PIXEL_FUNCTION static __m64 m64_cast_to(__m64 src) noexcept
            {
                constexpr bool include_alpha = has_alpha() and pixel<U>::has_alpha();
                constexpr std::array<int, 4> mul { U::bx, U::gx, U::rx, include_alpha ? U::ax : 0 };
                constexpr std::array<int, 4> div { P::bx, P::gx, P::rx, include_alpha ? P::ax : 1 };
                constexpr auto src_max = component_max(include_alpha);

                __m64 dst = mmx_muldiv_pu16<flags, true, mul, div, src_max>(src);

                if constexpr (pixel<U>::has_alpha() and not has_alpha())
                    dst = _mm_or_si64(dst, _mm_setr_pi16(0, 0, 0, U::ax));

                return dst;
            }

            template <typename U, typename VT>
            PIXEL_FUNCTION static constexpr simd_vector<VT, 4> vector_cast_to(simd_vector<VT, 4> src) noexcept
            {
                src *= pixel<U>::template vector_max<VT>();
                src = pixel<U>::template vector_div_max<VT>(src);
                if constexpr (pixel<U>::has_alpha() and not has_alpha())
                    src[3] = U::ax;
                return src;
            }

            PIXEL_FUNCTION static __m128 m128_premul(__m128 src) noexcept
            {
                if constexpr (not has_alpha()) return src;
                constexpr auto ax = reinterpret_cast<__m128>(1.0f / simd_vector<float, 4> { P::ax, P::ax, P::ax, 1 });
                auto srca = _mm_setr_ps(src[3], src[3], src[3], 1);
                src = _mm_mul_ps(src, srca);
                src = _mm_mul_ps(src, ax);
                return src;
            }

            template<simd flags>
            PIXEL_FUNCTION static __m64 m64_premul(__m64 src) noexcept
            {
                if constexpr (not has_alpha()) return src;
                constexpr std::uint16_t max = component_max(true);
                const std::uint16_t a = mmx_extract_pi16<flags, 3>(src);
                const __m64 va = mmx_shuffle<flags, shuffle_mask(3, 3, 3, 0)>(mmx_insert_pi16<flags, 0>(src, P::ax));
                src = _mm_mullo_pi16(src, va);
                src = mmx_div_scalar_pu16<flags, true, P::ax, max * P::ax>(src);
                return src;
            }

            template <typename VT>
            PIXEL_FUNCTION static constexpr simd_vector<VT, 4> vector_premul(simd_vector<VT, 4> src) noexcept
            {
                if constexpr (not has_alpha()) return src;
                auto a = simd_vector<VT, 4> { src[3], src[3], src[3], 1 };
                src *= a;
                return vector_divide_ax<VT>(src);
            }

            template <typename U>
            PIXEL_FUNCTION static __m128 m128_blend_premul(__m128 dst, __m128 src)
            {
                const __m128 sa = _mm_sub_ps(_mm_set1_ps(U::ax), _mm_set1_ps(src[3]));
                const float da = dst[3];

                if constexpr (not std::is_same_v<P, U>) src = pixel<U>::template m128_cast_to<P>(src);
                dst = _mm_mul_ps(dst, sa);
                if constexpr (U::ax != 1)
                {
                    constexpr auto ax = reinterpret_cast<__m128>(1.0f / simd_vector<float, 4> { U::ax, U::ax, U::ax, U::ax });
                    dst = _mm_mul_ps(dst, ax);
                }
                dst = _mm_add_ps(dst, src);
                if constexpr (has_alpha()) dst[3] = da;
                return dst;
            }

            template <typename U, simd flags>
            PIXEL_FUNCTION static __m64 m64_blend_premul(__m64 dst, __m64 src)
            {
                constexpr std::uint16_t max = component_max(false);
                const int sa = mmx_extract_pi16<flags, 3>(src);
                const int da = has_alpha() ? mmx_extract_pi16<flags, 3>(dst) : 0;

                if constexpr (not std::is_same_v<P, U>) src = pixel<U>::template m64_cast_to<P, flags>(src);

                constexpr std::array<int, 4> mul { 1, 1, 1, 0 };
                constexpr std::array<int, 4> div { U::ax, U::ax, U::ax, 1 };
                dst = _mm_mullo_pi16(dst, _mm_set1_pi16(U::ax - sa));
                dst = mmx_muldiv_pu16<flags, true, mul, div, max * U::ax>(dst);

                dst = _mm_adds_pu8(dst, src);

                if constexpr (has_alpha()) dst = mmx_insert_pi16<flags, 3>(dst, da);
                return dst;
            }

            template <typename U, typename VT>
            PIXEL_FUNCTION static constexpr simd_vector<VT, 4> vector_blend_premul(simd_vector<VT, 4> dst, simd_vector<VT, 4> src) noexcept
            {
                const auto sa = src[3];
                const auto da = dst[3];
                if constexpr (not std::is_same_v<P, U>) src = pixel<U>::template vector_cast_to<P, VT>(src);
                dst *= static_cast<VT>(U::ax - sa);
                dst = pixel<U>::template vector_div_ax<VT>(dst);
                dst += src;
                if constexpr (has_alpha()) dst[3] = da;
                return dst;
            }

            template <typename U>
            PIXEL_FUNCTION static __m128 m128_blend_straight(__m128 dst, __m128 src)
            {
                const __m128 sa = _mm_sub_ps(_mm_set1_ps(U::ax), _mm_set1_ps(src[3]));
                const float da = dst[3];

                if constexpr (not std::is_same_v<P, U>) src = pixel<U>::template m128_cast_to<P>(src);
                src = _mm_sub_ps(src, dst);
                src = _mm_mul_ps(src, sa);
                if constexpr (U::ax != 1)
                {
                    constexpr __m128 ax = 1.0f / __mm_set1_ps(U::ax);
                    src = _mm_mul_ps(src, ax);
                }
                dst = _mm_add_ps(dst, src);
                if constexpr (has_alpha()) dst[3] = da;
                return dst;
            }

            template <typename U, simd flags>
            PIXEL_FUNCTION static __m64 m64_blend_straight(__m64 dst, __m64 src)
            {
                constexpr unsigned max = component_max(false);
                const int sa = mmx_extract_pi16<flags, 3>(src);
                const int da = has_alpha() ? mmx_extract_pi16<flags, 3>(dst) : 0;

                if constexpr (not std::is_same_v<P, U>) src = pixel<U>::template m64_cast_to<P, flags>(src);

                src = _mm_mullo_pi16(src, _mm_set1_pi16(sa));
                dst = _mm_mullo_pi16(dst, _mm_set1_pi16(U::ax - sa));

                constexpr std::array<int, 4> mul { 1, 1, 1, 0 };
                constexpr std::array<int, 4> div { U::ax, U::ax, U::ax, 1 };
                src = mmx_muldiv_pu16<flags, true, mul, div, max * U::ax>(src);
                dst = mmx_muldiv_pu16<flags, true, mul, div, max * U::ax>(dst);

                dst = _mm_adds_pu8(dst, src);

                if constexpr (has_alpha()) dst = mmx_insert_pi16<flags, 3>(dst, da);
                return dst;
            }

            template <typename U, typename VT>
            PIXEL_FUNCTION static constexpr simd_vector<VT, 4> vector_blend_straight(simd_vector<VT, 4> dst, simd_vector<VT, 4> src) noexcept
            {
                const auto sa = src[3];
                const auto da = dst[3];
                if constexpr (not std::is_same_v<P, U>) src = pixel<U>::template vector_cast_to<P, VT>(src);
                src -= dst;
                src *= sa;
                src = pixel<U>::template vector_div_ax<VT>(src);
                dst += src;
                if constexpr (has_alpha()) dst[3] = da;
                return dst;
            }

            static consteval T component_max(bool with_alpha) noexcept
            {
                T x = std::max({ P::bx, P::gx, P::rx });
                if constexpr (has_alpha()) if (with_alpha) x = std::max(x, P::ax);
                return x;
            }

            static consteval T component_min(bool with_alpha) noexcept
            {
                T x = std::min({ P::bx, P::gx, P::rx });
                if constexpr (has_alpha()) if (with_alpha) x = std::min(x, P::ax);
                return x;
            }

            template<typename VT = float>
            static consteval auto vector_max(VT noalpha = 1) noexcept
            {
                return simd_vector<VT, 4> { P::bx, P::gx, P::rx, static_cast<VT>(has_alpha() ? P::ax : noalpha) };
            }

            template<typename VT, std::array<VT, 4> div>
            static constexpr auto vector_div(simd_vector<VT, 4> src)
            {
                if constexpr (std::is_floating_point_v<VT>)
                {
                    constexpr simd_vector<VT, 4> r = 1.0f / div;
                    src *= r;
                }
                else
                {
                    constexpr unsigned rbits = (sizeof(VT) - 1) * 8 - 1;
                    constexpr long double f = 1 << rbits;
                    constexpr simd_vector<VT, 4> r
                    {
                        static_cast<VT>(round(f / div[0])),
                        static_cast<VT>(round(f / div[1])),
                        static_cast<VT>(round(f / div[2])),
                        static_cast<VT>(round(f / div[3]))
                    };
                    src *= r;
                    src += 1 << (rbits - 1);
                    src >>= rbits;
                }
                return src;
            }

            template<typename VT>
            static constexpr auto vector_div_max(simd_vector<VT, 4> src)
            {
                constexpr std::array<VT, 4> div { P::bx, P::gx, P::rx, has_alpha() ? P::ax : 1 };
                return vector_div<VT, div>(src);
            }

            template<typename VT>
            static constexpr auto vector_div_ax(simd_vector<VT, 4> src)
            {
                constexpr std::array<VT, 4> div { P::ax, P::ax, P::ax, 1 };
                return vector_div<VT, div>(src);
            }

            static consteval std::uint8_t shuffle_mask(int v0, int v1, int v2, int v3) noexcept { return (v0 & 3) | ((v1 & 3) << 2) | ((v2 & 3) << 4) | ((v3 & 3) << 6); }
            static consteval bool byte_aligned() noexcept { return P::byte_aligned; }
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

        template<simd flags>
        inline auto generate_px8n_palette() noexcept
        {
            std::array<px32n, 256> result;
            for (unsigned i = 0; i < 256; ++i)
                result[i].template assign<flags>(*reinterpret_cast<px8n*>(&i));
            return result;
        }
    }
}
