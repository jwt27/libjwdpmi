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

            constexpr pixel(const pixel& p) noexcept { assign(p); }
            constexpr pixel(pixel&& p) noexcept { assign(p); }
            constexpr pixel& operator=(const pixel& p) noexcept { return assign(p); };
            constexpr pixel& operator=(pixel&& p) noexcept { return assign(p); };

            template <typename U> constexpr explicit operator pixel<U>() const noexcept { return cast_to<U, default_simd()>(); }

            static constexpr bool has_alpha() { return P::ax > 0; }

            template<simd flags = default_simd(), typename U>
            PIXEL_FUNCTION constexpr pixel& blend(const pixel<U>& other)
            {
                if constexpr (not pixel<U>::has_alpha())
                {
                    return *this = other.template cast_to<P>();
                }
                auto do_blend_vector = [this, &other]
                {
                    using VT = std::conditional_t<std::is_floating_point_v<typename P::T> or std::is_floating_point_v<typename U::T>, float, std::uint32_t>;
                    V<4, VT> src = other.template vector<VT>();
                    V<4, VT> dst = vector<VT>();
                    return *this = vector<VT>(vector_blend<U, VT>(dst, src));
                };

                if (std::is_constant_evaluated()) return do_blend_vector();

                if constexpr (flags.match(simd::sse) and (std::is_floating_point_v<typename P::T> or std::is_floating_point_v<typename U::T>))
                {
                    auto do_blend = [this, &other]()
                    {
                        return *this = m128(m128_blend<U>(m128(), other.m128()));
                    };
                    if constexpr (std::is_integral_v<typename P::T> or std::is_integral_v<typename U::T>)
                        return mmx_function<flags>([&do_blend] { return do_blend(); });
                    else return do_blend();
                }
                else if constexpr (flags.match(simd::mmx) and std::is_integral_v<typename P::T> and std::is_integral_v<typename U::T>)
                {
                    return mmx_function<flags>([this, &other] { return *this = m64(m64_blend<U, flags>(m64(), other.m64())); });
                }
                else return do_blend_vector();
            }

            template<simd flags = default_simd(), typename U>
            PIXEL_FUNCTION constexpr pixel& blend_straight(const pixel<U>& other)
            {
                if constexpr (not pixel<U>::has_alpha())
                {
                    return *this = other.template cast_to<P>();
                }

                auto do_blend_vector = [this, &other]
                {
                    using VT = std::conditional_t<std::is_floating_point_v<typename P::T> or std::is_floating_point_v<typename U::T>, float, std::uint32_t>;
                    V<4, VT> src = vector_premul<VT>(other.template vector<VT>());
                    V<4, VT> dst = vector_premul<VT>(vector<VT>());
                    *this = vector<VT>(vector_blend<U, VT>(dst, src));
                };

                if (std::is_constant_evaluated())
                {
                    do_blend_vector();
                }
                else if constexpr (flags.match(simd::sse) and (std::is_floating_point_v<typename P::T> or std::is_floating_point_v<typename U::T>))
                {
                    auto do_blend = [this, &other]()
                    {
                        *this = m128(m128_blend<U>(m128_premul(m128()), m128_premul(other.m128())));
                    };
                    if constexpr (std::is_integral_v<typename P::T> or std::is_integral_v<typename U::T>)
                        mmx_function<flags>([&do_blend] { do_blend(); });
                    else do_blend();
                }
                else if constexpr (flags.match(simd::mmx) and std::is_integral_v<typename P::T> and std::is_integral_v<typename U::T>)
                {
                    mmx_function<flags>([this, &other] { *this = m64(m64_blend<U, flags>(m64_premul<flags>(m64()), m64_premul<flags>(other.m64()))); });
                }
                else do_blend_vector();

                return *this;
            }

            template<simd flags = default_simd()>
            PIXEL_FUNCTION constexpr pixel& premultiply_alpha()
            {
                if constexpr (not has_alpha()) return *this;

                auto do_premul_vector = [this]
                {
                    using VT = std::conditional_t<std::is_floating_point_v<typename P::T>, float, std::uint32_t>;
                    return *this = vector<VT>(vector_premul<VT>(vector<VT>()));
                };

                if (std::is_constant_evaluated()) return do_premul_vector();

                if constexpr (flags.match(simd::sse) and std::is_floating_point_v<typename P::T>)* this = m128(m128_premul(m128()));
                else if constexpr (flags.match(simd::mmx) and not std::is_floating_point_v<typename P::T>)
                    return mmx_function<flags>([this] { return *this = m64(m64_premul<flags>(m64())); });
                else return do_premul_vector();
            }

            template <simd flags = default_simd(), typename U>
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
                    using VT = std::conditional_t<std::is_floating_point_v<typename P::T> or std::is_floating_point_v<typename U::T>, float, std::uint32_t>;
                    return pixel<U>::template vector<VT>(vector_cast_to<U, VT>(vector<VT>()));
                };

                if (std::is_constant_evaluated()) return do_cast_vector();

                if constexpr (flags.match(simd::sse) and (std::is_floating_point_v<typename P::T> or std::is_floating_point_v<typename U::T>))
                {
                    auto do_cast = [this]()
                    {
                        return pixel<U>::m128(m128_cast_to<U>(m128()));
                    };
                    if constexpr (std::is_integral_v<typename P::T> or std::is_integral_v<typename U::T>)
                        return mmx_function<flags>([&do_cast] { return do_cast(); });
                    else return do_cast();
                }
                else if constexpr (flags.match(simd::mmx) and (flags.match(simd::sse) or (std::is_integral_v<typename P::T> and std::is_integral_v<typename U::T>)))
                {
                    return mmx_function<flags>([this] { return pixel<U>::m64(m64_cast_to<U, flags>(m64())); });
                }
                else return do_cast_vector();
            }

            PIXEL_FUNCTION static constexpr pixel m64(__m64 value) noexcept // V4HI
            {
                static_assert(not std::is_floating_point_v<typename P::T>);
                auto v = _mm_packs_pu16(value, _mm_setzero_si64());
                if constexpr (byte_aligned())
                {
                    auto v2 = _mm_cvtsi64_si32(v);
                    pixel result { *reinterpret_cast<pixel*>(&v2) };
                    return result;
                }
                else
                {
                    auto v2 = reinterpret_cast<simd_vector<std::uint8_t, 8>&>(v);
                    pixel result { v2[2], v2[1], v2[0], v2[3] };
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
            PIXEL_FUNCTION static constexpr pixel vector(simd_vector<VT, 4> src) noexcept
            {
                if constexpr ((std::is_same_v<VT, float> and std::is_same_v<typename P::T, float>) or (sizeof(VT) == 1 and byte_aligned()))
                {
                    return *reinterpret_cast<pixel*>(&src);
                }
                return pixel { src[2], src[1], src[0], src[3] };
            }

            template<typename VT = std::uint16_t>
            PIXEL_FUNCTION constexpr simd_vector<VT, 4> vector() const noexcept
            {
                simd_vector<VT, 4> src;
                if constexpr ((std::is_same_v<VT, float> and std::is_same_v<typename P::T, float>) or (sizeof(VT) == 1 and byte_aligned()))
                    src = *reinterpret_cast<const simd_vector<VT, 4>*>(this);
                else if constexpr (has_alpha())
                    src = simd_vector<VT, 4> { static_cast<VT>(this->b), static_cast<VT>(this->g), static_cast<VT>(this->r), static_cast<VT>(this->a), };
                else src = simd_vector<VT, 4> { static_cast<VT>(this->b), static_cast<VT>(this->g), static_cast<VT>(this->r), 0 };
                return src;
            }

            template <typename U>
            PIXEL_FUNCTION static constexpr __m128 m128_cast_to(__m128 src) noexcept
            {
                constexpr __m128 cast = reinterpret_cast<__m128>(pixel<U>::template vector_max<float>(P::ax) * (1.0f / vector_max<float>(U::ax > 0 ? U::ax : 1.0f)));
                src = _mm_mul_ps(src, cast);
                if constexpr (pixel<U>::has_alpha() and not has_alpha())
                    src = _mm_setr_ps(src[0], src[1], src[2], static_cast<float>(U::ax));
                return src;
            }

            template <typename U, simd flags>
            PIXEL_FUNCTION static constexpr __m64 m64_cast_to(__m64 src) noexcept
            {
                constexpr bool include_alpha = has_alpha() and pixel<U>::has_alpha();
                constexpr std::array<int, 4> mul { U::bx, U::gx, U::rx, include_alpha ? U::ax : 0 };
                constexpr std::array<int, 4> div { P::bx, P::gx, P::rx, include_alpha ? P::ax : 1 };

                constexpr auto src_max = component_max(include_alpha);

                __m64 dst = mmx_muldiv_pu16<flags, true, mul, div, src_max>(src);

                if constexpr (pixel<U>::has_alpha() and not has_alpha())
                    dst = mmx_insert_constant_pi16<flags, 3, U::ax>(dst);

                return dst;
            }

            template <typename U, typename VT>
            PIXEL_FUNCTION static constexpr simd_vector<VT, 4> vector_cast_to(simd_vector<VT, 4> src) noexcept
            {
                if constexpr (std::is_floating_point_v<VT>)
                {
                    src *= pixel<U>::template vector_max<VT>(P::ax) * (1.0f / vector_max<VT>(U::ax > 0 ? U::ax : 1.0f));
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
                else return simd_vector<VT, 4> { src[0], src[1], src[2], static_cast<VT>(U::ax) };
            }

            PIXEL_FUNCTION static constexpr __m128 m128_premul(__m128 src) noexcept
            {
                if constexpr (not has_alpha()) return src;
                constexpr auto ax = reinterpret_cast<__m128>(1.0f / simd_vector<float, 4> { P::ax, P::ax, P::ax, 1 });
                auto srca = _mm_setr_ps(src[3], src[3], src[3], 1);
                src = _mm_mul_ps(src, srca);
                src = _mm_mul_ps(src, ax);
                return src;
            }

            template<simd flags>
            PIXEL_FUNCTION static constexpr __m64 m64_premul(__m64 src) noexcept
            {
                if constexpr (not has_alpha()) return src;
                constexpr std::uint16_t max = std::max({ P::bx, P::gx, P::rx, P::ax });
                const std::uint16_t a = mmx_extract_pi16<flags, 3>(src);
                const __m64 va = _mm_setr_pi16(a, a, a, P::ax);
                src = _mm_mullo_pi16(src, va);
                src = mmx_div_scalar_pu16<flags, true, P::ax, max * P::ax>(src);
                src = mmx_insert_pi16<flags, 3>(src, a);
                return src;
            }

            template <typename VT>
            PIXEL_FUNCTION static constexpr simd_vector<VT, 4> vector_premul(simd_vector<VT, 4> src) noexcept
            {
                if constexpr (not has_alpha()) return src;
                auto a = simd_vector<VT, 4> { src[3], src[3], src[3], 1 };
                if constexpr (std::is_floating_point_v<VT>)
                {
                    constexpr auto ax = 1.0f / simd_vector<float, 4> { P::ax, P::ax, P::ax, 1 };
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
                auto a = _mm_sub_ps(_mm_set1_ps(U::ax), _mm_set1_ps(src[3]));

                if constexpr (not std::is_same_v<P, U>) src = pixel<U>::template m128_cast_to<P>(src);
                dst = _mm_mul_ps(dst, a);
                if constexpr (U::ax != 1)
                {
                    constexpr auto ax = reinterpret_cast<__m128>(1.0f / simd_vector<float, 4> { U::ax, U::ax, U::ax, U::ax });
                    dst = _mm_mul_ps(dst, ax);
                }
                dst = _mm_add_ps(dst, src);
                return dst;
            }

            template <typename U, simd flags>
            PIXEL_FUNCTION constexpr __m64 m64_blend(__m64 dst, __m64 src)
            {
                constexpr std::uint16_t max = std::max({ P::bx, P::gx, P::rx, P::ax });
                auto a = _mm_set1_pi16(U::ax - mmx_extract_pi16<flags, 3>(src));
                if constexpr (not std::is_same_v<P, U>) src = pixel<U>::template m64_cast_to<P, flags>(src);
                dst = _mm_mullo_pi16(dst, a);
                dst = mmx_div_scalar_pu16<flags, true, U::ax, max * U::ax>(dst);
                dst = _mm_adds_pu8(dst, src);
                return dst;
            }

            template <typename U, typename VT>
            PIXEL_FUNCTION static constexpr simd_vector<VT, 4> vector_blend(simd_vector<VT, 4> dst, simd_vector<VT, 4> src) noexcept
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

            template<std::size_t bits, typename VT = std::uint16_t, std::size_t maxbits = bits>
            static consteval auto vector_reciprocal(VT v0, VT v1, VT v2, VT v3) noexcept
            {
                auto r = [](VT v) -> VT { return std::min(((1ul << bits) + v - 1) / v, (1ul << maxbits) - 1); };
                return simd_vector<VT, 4> { r(v0), r(v1), r(v2), r(v3) };
            }

            template<std::size_t bits, typename VT = std::uint16_t, std::size_t maxbits = bits>
            static consteval auto vector_reciprocal(VT v0) noexcept
            {
                return vector_reciprocal<bits, VT, maxbits>(v0, v0, v0, v0);
            }

            template<typename VT = float>
            static consteval auto vector_max(VT noalpha = 1) noexcept
            {
                return simd_vector<VT, 4> { P::bx, P::gx, P::rx, static_cast<VT>(has_alpha() ? P::ax : noalpha) };
            }

            template<std::size_t bits, typename VT = std::uint16_t, std::size_t maxbits = bits>
            static consteval auto vector_max_reciprocal(VT noalpha = 1) noexcept
            {
                return vector_reciprocal<bits, VT, maxbits>(P::bx, P::gx, P::rx, static_cast<VT>(has_alpha() ? P::ax : noalpha));
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

        inline auto generate_px8n_palette() noexcept
        {
            std::array<px32n, 256> result;
            for (unsigned i = 0; i < 256; ++i)
                result[i] = static_cast<px32n>(*reinterpret_cast<px8n*>(&i));
            return result;
        }
    }
}
