/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <type_traits>
#include <concepts>
#include <xmmintrin.h>
#include <mm3dnow.h>
#include <jw/simd_flags.h>

namespace jw
{
    template<typename T, std::size_t N>
    using simd_vector [[gnu::vector_size(sizeof(T) * N)]] = T;

    struct shuffle_mask
    {
        std::uint8_t mask;

        constexpr shuffle_mask(std::uint8_t mask) : mask { mask } { }
        constexpr shuffle_mask(unsigned v0, unsigned v1, unsigned v2, unsigned v3)
            : mask { static_cast<std::uint8_t>(((v3 & 3) << 6) | ((v2 & 3) << 4) | ((v1 & 3) << 2) | (v0 & 3)) } { }

        constexpr operator std::uint8_t() const noexcept { return mask; }
        constexpr unsigned operator[](unsigned i) const noexcept { return (mask >> (i << 1)) & 3; }
    };

    // Using these types in template parameters (std::same_as, etc) prevents
    // ignored-attribute warnings.
    using m64_t = simd_vector<int, 2>;
    using m128_t = simd_vector<float, 4>;

    struct format_nosimd { } inline constexpr nosimd;
    struct format_pi8    { } inline constexpr pi8;
    struct format_pi16   { } inline constexpr pi16;
    struct format_pi32   { } inline constexpr pi32;
    struct format_si64   { } inline constexpr si64;
    struct format_ps     { } inline constexpr ps;
    struct format_pf     { } inline constexpr pf;

    template<typename T, typename... U>
    concept any_of = (std::same_as<T, U> or ...);

    template<typename T>
    concept simd_format = any_of<T, format_nosimd, format_pi8, format_pi16, format_pi32, format_si64, format_ps, format_pf>;

    template<typename T, typename... U>
    concept any_simd_format_of = simd_format<T> and (simd_format<U> and ...) and any_of<T, U...>;

    template<typename T, typename... U>
    concept any_simd_format_but = simd_format<T> and (simd_format<U> and ...) and not any_of<T, U...>;

    template<simd_format>
    struct simd_format_traits
    {
        using type = void;
        static constexpr simd flags = simd::none;
        static constexpr std::size_t elements = 1;
        static constexpr std::size_t element_size = 0;
    };

    template<>
    struct simd_format_traits<format_pi8>
    {
        using type = m64_t;
        static constexpr simd flags = simd::mmx;
        static constexpr std::size_t elements = 8;
        static constexpr std::size_t element_size = 1;
    };

    template<>
    struct simd_format_traits<format_pi16>
    {
        using type = m64_t;
        static constexpr simd flags = simd::mmx;
        static constexpr std::size_t elements = 4;
        static constexpr std::size_t element_size = 2;
    };

    template<>
    struct simd_format_traits<format_pi32>
    {
        using type = m64_t;
        static constexpr simd flags = simd::mmx;
        static constexpr std::size_t elements = 2;
        static constexpr std::size_t element_size = 4;
    };

    template<>
    struct simd_format_traits<format_si64>
    {
        using type = m64_t;
        static constexpr simd flags = simd::mmx;
        static constexpr std::size_t elements = 1;
        static constexpr std::size_t element_size = 8;
    };

    template<>
    struct simd_format_traits<format_ps>
    {
        using type = m128_t;
        static constexpr simd flags = simd::sse;
        static constexpr std::size_t elements = 4;
        static constexpr std::size_t element_size = 4;
    };

    template<>
    struct simd_format_traits<format_pf>
    {
        using type = m64_t;
        static constexpr simd flags = simd::amd3dnow;
        static constexpr std::size_t elements = 2;
        static constexpr std::size_t element_size = 4;
    };

    // Specialize this for custom types.
    template<typename T, simd_format F>
    struct simd_type_traits
    {
        // Data type to represent this type in the given format.
        using data_type = typename simd_format_traits<F>::type;

        // Advance iterators by this amount.
        static constexpr std::size_t delta = simd_format_traits<F>::elements;
    };

    template<typename T> requires (std::is_arithmetic_v<T>)
    struct simd_type_traits<T, format_nosimd>
    {
        using data_type = T;
        static constexpr std::size_t delta = 1;
    };

    // Check if the given type was produced by simd_data().
    template<typename T> concept simd_data_type = requires (T r)
    {
        typename T::type;
        typename T::data_type;
        { r.data } -> std::convertible_to<typename T::data_type>;
    };

    // Check if the given type was produced by simd_return().
    template<typename T> concept simd_return_type = requires (T r)
    {
        typename T::format;
        requires simd_format<typename T::format>;
        { r.data };
        { std::tuple_size_v<decltype(r.data)> } -> std::convertible_to<std::size_t>;
    };

    // Read the type name stored by simd_data().
    template<typename D> using simd_type = typename std::decay_t<D>::type;

    // Check if the simd_type of D matches T.
    template<typename D, typename T>
    concept simd_data_for = simd_data_type<D> and std::same_as<simd_type<D>, T>;

    template<typename I, simd flags, typename Fmt> concept simd_loadable = requires (Fmt t, I p) { { simd_load<flags>(t, p) } -> std::same_as<typename simd_type_traits<std::remove_cv_t<std::iter_value_t<I>>, Fmt>::data_type>; };
    template<typename O, simd flags, typename Fmt> concept simd_storable = requires (Fmt t, O p, typename simd_type_traits<std::remove_cv_t<std::iter_value_t<O>>, Fmt>::data_type v) { simd_store<flags>(t, p, v); };

    template<simd, std::indirectly_readable I> requires (std::is_arithmetic_v<std::iter_value_t<I>>)
    [[gnu::always_inline]] inline auto simd_load(format_nosimd, I src)
    {
        return *src;
    }

    template<simd, typename T, std::indirectly_writable<T> O> requires (std::is_arithmetic_v<std::iter_value_t<O>>)
    [[gnu::always_inline]] inline void simd_store(format_nosimd, O dst, T src)
    {
        *dst = src;
    }

    template<simd, std::contiguous_iterator I> requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) == 1)
    [[gnu::always_inline]] inline __m64 simd_load(format_pi8, I src)
    {
        return *reinterpret_cast<const __m64*>(&*src);
    }

    template<simd, std::contiguous_iterator I> requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) == 1)
    [[gnu::always_inline]] inline void simd_store(format_pi8, I dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(&*dst) = src;
    }

    template<simd flags, std::contiguous_iterator I> requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) <= 2)
    [[gnu::always_inline]] inline __m64 simd_load(format_pi16, const I src)
    {
        if constexpr (sizeof(std::iter_value_t<I>) == 2) return *reinterpret_cast<const __m64*>(&*src);
        else
        {
            __m64 data = simd_load<flags>(pi8, src);
            __m64 sign = _mm_setzero_si64();
            if constexpr (std::is_signed_v<std::iter_value_t<I>>) sign = _mm_cmpgt_pi8(sign, data);
            data = _mm_unpacklo_pi8(data, sign);
            return data;
        }
    }

    template<simd, std::contiguous_iterator I> requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) == 2)
    [[gnu::always_inline]] inline void simd_store(format_pi16, I dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(&*dst) = src;
    }

    template<simd, std::contiguous_iterator I> requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) == 1)
    [[gnu::always_inline]] inline void simd_store(format_pi16, I dst, __m64 src)
    {
        const __m64 a = std::is_signed_v<std::iter_value_t<I>> ? _mm_packs_pi16(src, src) : _mm_packs_pu16(src, src);
        *reinterpret_cast<std::uint32_t*>(&*dst) = _mm_cvtsi64_si32(a);
    }

    template<simd flags, std::contiguous_iterator I> requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) <= 4)
    [[gnu::always_inline]] inline __m64 simd_load(format_pi32, const I src)
    {
        if constexpr (sizeof(std::iter_value_t<I>) == 4) return *reinterpret_cast<const __m64*>(&*src);
        else
        {
            __m64 data = simd_load<flags>(pi16, src);
            __m64 sign = _mm_setzero_si64();
            if constexpr (std::is_signed_v<std::iter_value_t<I>>) sign = _mm_cmpgt_pi16(sign, data);
            data = _mm_unpacklo_pi16(data, sign);
            return data;
        }
    }

    template<simd, std::contiguous_iterator I> requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) == 4)
    [[gnu::always_inline]] inline void simd_store(format_pi32, I dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(&*dst) = src;
    }

    template<simd, std::contiguous_iterator I> requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) == 2)
    [[gnu::always_inline]] inline void simd_store(format_pi32, I dst, __m64 src)
    {
        const __m64 a = _mm_packs_pi32(src, src);
        *reinterpret_cast<std::uint32_t*>(&*dst) = _mm_cvtsi64_si32(a);
    }

    template<simd, std::contiguous_iterator I> requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) == 1)
    [[gnu::always_inline]] inline void simd_store(format_pi32, I dst, __m64 src)
    {
        __m64 data = _mm_packs_pi32(src, src);
        data = std::is_signed_v<std::iter_value_t<I>> ? _mm_packs_pi16(data, data) : _mm_packs_pu16(data, data);
        *reinterpret_cast<std::uint16_t*>(&*dst) = _mm_cvtsi64_si32(data);
    }

    template<simd flags, std::contiguous_iterator I> requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) <= 8)
    [[gnu::always_inline]] inline __m64 simd_load(format_si64, const I src)
    {
        if constexpr (sizeof(std::iter_value_t<I>) == 8) return *reinterpret_cast<const __m64*>(&*src);
        else
        {
            __m64 data = simd_load<flags>(pi32, src);
            __m64 sign = _mm_setzero_si64();
            if constexpr (std::is_signed_v<std::iter_value_t<I>>) sign = _mm_cmpgt_pi32(sign, data);
            data = _mm_unpacklo_pi32(data, sign);
            return data;
        }
    }

    template<simd, typename I> requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) == 8)
    [[gnu::always_inline]] inline void simd_store(format_si64, I dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(&*dst) = src;
    }

    template<simd, std::contiguous_iterator I> requires (std::same_as<std::iter_value_t<I>, float>)
    [[gnu::always_inline]] inline __m128 simd_load(format_ps, I src)
    {
        return *reinterpret_cast<const __m128*>(&*src);
    }

    template<simd, std::contiguous_iterator I> requires (std::signed_integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) == 4)
    [[gnu::always_inline]] inline __m128 simd_load(format_ps, I src)
    {
        const __m64 lo = *reinterpret_cast<const __m64*>(&*(src + 0));
        const __m64 hi = *reinterpret_cast<const __m64*>(&*(src + 2));
        return _mm_cvtpi32x2_ps(lo, hi);
    }

    template<simd flags, std::contiguous_iterator I> requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) <= 2)
    [[gnu::always_inline]] inline __m128 simd_load(format_ps, I src)
    {
        const __m64 data = simd_load<flags>(pi16, src);
        return std::is_signed_v<std::iter_value_t<I>> ? _mm_cvtpi16_ps(data) : _mm_cvtpu16_ps(data);
    }

    template<simd, std::contiguous_iterator I> requires (std::same_as<std::iter_value_t<I>, float>)
    [[gnu::always_inline]] inline void simd_store(format_ps, I dst, __m128 src)
    {
        *reinterpret_cast<__m128*>(&*dst) = src;
    }

    template<simd flags, std::contiguous_iterator I>
    requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) <= 4 and (std::is_signed_v<std::iter_value_t<I>> or sizeof(std::iter_value_t<I>) == 1))
    [[gnu::always_inline]] inline void simd_store(format_ps, I dst, __m128 src)
    {
        const __m64 lo = _mm_cvtps_pi32(src);
        const __m64 hi = _mm_cvtps_pi32(_mm_movehl_ps(src, src));
        if constexpr (simd_storable<I, flags, format_pi16>)
            simd_store<flags>(pi16, dst, _mm_packs_pi32(lo, hi));
        else
        {
            simd_store<flags>(pi32, dst + 0, lo);
            simd_store<flags>(pi32, dst + 2, hi);
        }
    }

    template<simd, std::contiguous_iterator I> requires (std::same_as<std::iter_value_t<I>, float>)
    [[gnu::always_inline]] inline __m64 simd_load(format_pf, I src)
    {
        return *reinterpret_cast<const __m64*>(&*src);
    }

    template<simd flags, std::contiguous_iterator I>
    requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) <= 4 and (std::is_signed_v<std::iter_value_t<I>> or sizeof(std::iter_value_t<I>) <= 2))
    [[gnu::always_inline]] inline __m64 simd_load(format_pf, const I src)
    {
        return _m_pi2fd(simd_load<flags>(pi32, src));
    }

    template<simd, std::contiguous_iterator I> requires (std::same_as<std::iter_value_t<I>, float>)
    [[gnu::always_inline]] inline void simd_store(format_pf, I dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(&*dst) = src;
    }

    template<simd flags, std::contiguous_iterator I>
    requires (std::integral<std::iter_value_t<I>> and sizeof(std::iter_value_t<I>) <= 4 and (std::is_signed_v<std::iter_value_t<I>> or sizeof(std::iter_value_t<I>) == 1))
    [[gnu::always_inline]] inline void simd_store(format_pf, I dst, __m64 src)
    {
        simd_store<flags>(pi32, dst, _m_pf2id(src));
    }

    template<typename F, simd flags, typename... A>
    concept simd_invocable = requires(F&& f, A&&... args) { std::forward<F>(f).template operator()<flags>(std::forward<A>(args)...); };

    template<simd flags, typename F, typename... A>
    [[gnu::flatten]] decltype(auto) simd_invoke(F&& func, A&&... args)
    {
        return (std::forward<F>(func).template operator()<flags>(std::forward<A>(args)...));
    }

    template<typename F, simd flags, typename... A>
    using simd_invoke_result = decltype(simd_invoke<flags>(std::declval<F>(), std::declval<A>()...));

#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wunused-local-typedefs"

    // Wrap data in a struct with the specified type info.
    template <typename T, typename D>
    auto simd_data(D&& data)
    {
        struct impl
        {
            using type = std::remove_cvref_t<T>;
            using data_type = std::decay_t<D>;
            const data_type data;

            constexpr operator data_type() const noexcept { return data; }
            constexpr const data_type& get() const noexcept { return data; }
        };
        if constexpr (simd_data_type<std::decay_t<D>>)
            return simd_data<T, typename std::decay_t<D>::data_type>(std::forward<D>(data));
        else
            return impl { std::forward<D>(data) };
    }

    // Wrap simd_data in a struct with the specified format info.  All data is
    // converted to the data_type specified in simd_type_traits.
    template<simd_format F, typename... D>
    auto simd_return(F, D&&... data)
    {
        auto make = []<typename... D2>(D2&&... data)
        {
            struct impl
            {
                using format = F;
                std::tuple<std::decay_t<D2>...> data;
            };
            return impl { std::tuple<std::decay_t<D2>...> { std::forward<D2>(data)... } };
        };
        return make(simd_data<simd_type<D>>(static_cast<simd_type_traits<simd_type<D>, F>::data_type>(std::forward<D>(data).data))...);
    }

#   pragma GCC diagnostic pop

    // Increment an iterator by the amount specified in simd_type_traits.
    template<simd_format F, typename I>
    inline void increment_simd_iterator(I* i) noexcept
    {
        constexpr auto delta = simd_type_traits<std::iter_value_t<I>, F>::delta;
        if constexpr (delta == 1) ++*i;
        else *i += delta;
    }

    // Load from one or more iterators, passed by pointer, and return
    // simd_data.  The iterators are incremented by the amount specified in
    // simd_type_traits.
    struct simd_source
    {
        template<simd flags, simd_format F, typename... I> requires (simd_loadable<I, flags, F> and ...)
        auto operator()(F, I*... iterators)
        {
            auto ret = simd_return(F { }, simd_data<std::iter_value_t<I>>(simd_load<flags>(F { }, *iterators))...);
            (increment_simd_iterator<F>(iterators), ...);
            return ret;
        }
    };

    // Store the incoming simd_data in a contained set of iterators.
    template<typename... I>
    struct simd_sink
    {
        constexpr simd_sink(I... i) : iterators { i... } { }

        template<simd flags, simd_format F, typename... D>
        requires (sizeof...(D) == sizeof...(I) and
                  (simd_storable<I, flags, F> and ...) and
                  (std::output_iterator<I, simd_type<D>> and ...))
        void operator()(F, D&&... data)
        {
            store<flags>(F { }, std::index_sequence_for<D...> { }, std::forward_as_tuple(std::forward<D>(data)...));
        }

    private:
        template<simd flags, simd_format F, std::size_t... N>
        void store(F, std::index_sequence<N...>, auto data)
        {
            (simd_store<flags>(F { }, std::get<N>(iterators), std::get<N>(data)), ...);
            (increment_simd_iterator<F>(&std::get<N>(iterators)), ...);
        }

        std::tuple<I...> iterators;
    };

    // Convert input directly to SIMD data via simd_load.  This is only
    // possible for types where the returned SIMD vector represents one
    // element of T.  For regular arithmetic types, only format_nosimd
    // satisfies this constraint.
    struct simd_in
    {
        template<simd flags, simd_format F, typename T>
        requires (simd_loadable<const T*, flags, F> and simd_type_traits<T, F>::delta == 1)
        auto operator()(F, const T& value)
        {
            return simd_data<T>(simd_load<flags>(F { }, &value));
        }
    };

    // Convert SIMD data directly to output value via simd_store.  As with
    // simd_in, this only possible when the SIMD vector represents a single
    // element of the output type.
    struct simd_out
    {
        template<simd flags, simd_format F, simd_data_type D>
        requires (simd_storable<simd_type<D>*, flags, F> and simd_type_traits<simd_type<D>, F>::delta == 1
                  and std::is_default_constructible_v<simd_type<D>>)
        auto operator()(F, D data)
        {
            simd_type<D> value;
            simd_store<flags>(F { }, &value, data);
            return value;
        }
    };

    // Reinterpret simd_data as a different type.
    template<typename... T>
    struct simd_reinterpret
    {
        template<simd flags, typename... U> requires (sizeof...(T) == sizeof...(U))
            auto operator()(auto fmt, U&&... data)
        {
            return simd_return(fmt, simd_data<T>(std::forward<U>(data))...);
        }
    };

    // A SIMD pipeline is composed of one or more functor objects, each of
    // which defines an operator() with the following signature:
    //  template<simd flags> auto operator()(simd_format fmt, auto... src)
    // The input data is wrapped by simd_data() so that it encodes the type of
    // data that is being operated on.  This can be recovered via simd_type.
    // Data is then returned via simd_return(fmt, simd_data<T>(dst)...), which
    // is passed on to the next stage.  If a stage only accepts a single
    // input, and does not change its format or type, the result may be
    // returned directly.  The first and last stages may also accept/return
    // arbitrary types.
    // When a stage produces more data than the next stage can accept, that
    // next stage is invoked multiple times.
    template<typename... T>
    class simd_pipeline
    {
        std::tuple<T...> stages;

        template<typename... U> using tuple_id = std::type_identity<std::tuple<U...>>;

        template<simd flags, simd_format Fmt, std::size_t stage, std::size_t first_arg, typename RFmt, typename R, typename... A>
        static consteval bool invocable_recurse(R result, tuple_id<A...> args, auto seq)
        {
            if constexpr (first_arg < sizeof...(A))         // Check next args slice on same stage
                return check_args_slice<flags, Fmt, stage, first_arg, RFmt>(result, args, seq);
            else if constexpr (stage + 1 < sizeof...(T))    // Done, check next stage
            {
                static_assert (std::tuple_size_v<typename R::type> != 0, "Pipeline stage returns nothing");
                return invocable<flags, RFmt, stage + 1>(result);
            }
            else return true;
        }

        template<simd flags, simd_format Fmt, std::size_t stage, std::size_t first_arg, typename... A, std::size_t... N>
        static consteval auto sliced_invoke_result(tuple_id<A...>, std::index_sequence<N...>)
        {
            using func = std::tuple_element_t<stage, decltype(stages)>;
            using slice = std::tuple<std::tuple_element_t<first_arg + N, std::tuple<A...>>...>;
            constexpr auto check = []<typename... A2>(tuple_id<A2...>)
            {
                if constexpr (simd_invocable<func, flags, Fmt, A2...>)
                    return std::type_identity<simd_invoke_result<func, flags, Fmt, A2...>> { };
            };
            return check(std::type_identity<slice> { });
        }

        template<simd flags, simd_format Fmt, std::size_t stage, std::size_t first_arg, typename RFmt, typename... R, typename... A, std::size_t... N>
        static consteval bool check_args_slice(tuple_id<R...>, tuple_id<A...> args, std::index_sequence<N...> seq)
        {
            using result_id = decltype(sliced_invoke_result<flags, Fmt, stage, first_arg>(args, seq));
            if constexpr (not std::same_as<result_id, void>)
            {
                constexpr auto next_arg = first_arg + sizeof...(N);
                using result = typename result_id::type;
                if constexpr (simd_return_type<result>)
                {
                    // This stage returns via simd_return(fmt, simd_data<T>(result)...)
                    using r_format = typename result::format;
                    using r_data = decltype(result::data);
                    using cumulative_data = decltype(std::tuple_cat(std::declval<std::tuple<R...>>(), std::declval<r_data>()));
                    static_assert (std::same_as<RFmt, void> or std::same_as<RFmt, r_format>, "Pipeline stage returns conflicting formats");
                    return invocable_recurse<flags, Fmt, stage, next_arg, r_format>(std::type_identity<cumulative_data> { }, args, seq);
                }
                else
                {
                    static_assert (stage == sizeof...(T) - 1 or (sizeof...(N) == 1 and not std::same_as<result, void>),
                                   "Intermediate pipeline stages with multiple inputs must return via simd_return()");

                    if constexpr (not std::same_as<result, void>)
                    {
                        if constexpr (stage + 1 < sizeof...(T))
                        {
                            static_assert (stage > 0 or simd_data_type<result>, "First stage must return via simd_data()");
                            constexpr auto check_arg_type = []
                            {
                                // Get the simd_type of the argument (only one is allowed here)
                                using arg_type = std::tuple_element_t<first_arg, std::tuple<A...>>;
                                if constexpr (simd_data_type<arg_type>) return std::type_identity<simd_type<arg_type>> { };
                                else return std::type_identity<std::remove_cvref_t<arg_type>> { };
                            };
                            using arg_type = decltype(check_arg_type())::type;

                            constexpr auto wrap_result = []
                            {
                                // Wrap result in simd_data() if not done already
                                if constexpr (simd_data_type<result>) return std::type_identity<result> { };
                                else return std::type_identity<decltype(simd_data<arg_type>(std::declval<result>()))> { };
                            };
                            using wrapped_result = decltype(wrap_result())::type;

                            // Run result through simd_return() to convert it to the type specified in simd_type_traits
                            using converted_result = std::tuple_element_t<0, decltype(simd_return(std::declval<Fmt>(), std::declval<wrapped_result>()).data)>;

                            return invocable_recurse<flags, Fmt, stage, next_arg, Fmt>(tuple_id<R..., converted_result> { }, args, seq);
                        }
                        else return invocable_recurse<flags, Fmt, stage, next_arg, void>(tuple_id<R..., result> { }, args, seq);
                    }
                    else return invocable_recurse<flags, Fmt, stage, next_arg, void>(tuple_id<> { }, args, seq);
                }
            }
            else return false;
        }

        template<simd flags, simd_format Fmt, std::size_t stage, std::size_t try_args, typename... A>
        static consteval std::size_t args_slice_size(tuple_id<A...> args)
        {
            if constexpr (try_args == 0) return 0;
            else if constexpr (sizeof...(A) % try_args == 0)
            {
                if constexpr (check_args_slice<flags, Fmt, stage, 0, void>(tuple_id<> { }, args, std::make_index_sequence<try_args> { }))
                    return try_args;
                else return args_slice_size<flags, Fmt, stage, try_args - 1>(args);
            }
            else return args_slice_size<flags, Fmt, stage, try_args - 1>(args);
        }

        template<simd flags, simd_format Fmt, std::size_t stage = 0, typename... A>
        static consteval bool invocable(tuple_id<A...> args)
        {
            if constexpr (not flags.match(simd_format_traits<Fmt>::flags)) return false;
            else return args_slice_size<flags, Fmt, stage, sizeof...(A)>(args) > 0;
        }

        template<simd_format Fmt, typename... R>
        auto make_result(std::tuple<R...>&& result)
        {
            auto unpack = [&result]<std::size_t... N>(std::index_sequence<N...>)
            {
                return simd_return(Fmt { }, std::get<N>(std::move(result))...);
            };
            return unpack(std::index_sequence_for<R...> { });
        }

        template<simd flags, simd_format Fmt, typename RFmt, std::size_t stage, std::size_t first_arg = 0, typename... A, typename R>
        auto invoke_recurse(std::tuple<A...>&& args, auto seq, R&& result_so_far)
        {
            if constexpr (first_arg < sizeof...(A))
                return invoke_slice<flags, Fmt, stage, first_arg>(std::move(args), seq, std::forward<R>(result_so_far));
            else if constexpr (std::same_as<RFmt, void>)
            {
                if constexpr (std::tuple_size_v<R> == 0) return;
                else if constexpr (std::tuple_size_v<R> == 1) return std::get<0>(result_so_far);
                else return result_so_far;
            }
            else return make_result<RFmt>(std::forward<R>(result_so_far));
        }

        template<simd flags, simd_format Fmt, std::size_t stage, std::size_t first_arg = 0, typename... A, typename... R, std::size_t... N>
        auto invoke_slice(std::tuple<A...>&& args, std::index_sequence<N...> seq, std::tuple<R...> result_so_far)
        {
            constexpr auto next_arg = first_arg + sizeof...(N);
            using result_id = decltype(sliced_invoke_result<flags, Fmt, stage, first_arg>(tuple_id<A...> { }, seq));
            auto do_invoke = [&args, this]
            {
                return simd_invoke<flags>(std::get<stage>(stages), Fmt { }, std::forward<std::tuple_element_t<first_arg + N, std::tuple<A...>>>(std::get<first_arg + N>(std::move(args)))...);
            };

            if constexpr (std::same_as<typename result_id::type, void>)
            {
                do_invoke();
                return invoke_recurse<flags, Fmt, void, stage, next_arg>(std::move(args), seq, std::tuple<> { });
            }
            else if constexpr (stage + 1 < sizeof...(T))
            {
                using result_type = typename result_id::type;
                auto wrap_invoke = [&do_invoke]
                {
                    if constexpr (not simd_return_type<result_type>)
                    {
                        if constexpr (not simd_data_type<result_type>)
                            return simd_return(Fmt { }, simd_data<simd_type<std::tuple_element_t<first_arg, std::tuple<A...>>>>(do_invoke()));
                        else
                            return simd_return(Fmt { }, do_invoke());
                    }
                    else return do_invoke();
                };
                auto result = wrap_invoke();
                using r_format = typename decltype(result)::format;
                return invoke_recurse<flags, Fmt, r_format, stage, next_arg>(std::move(args), seq, std::tuple_cat(std::move(result_so_far), std::move(result.data)));
            }
            else
            {
                auto result = std::make_tuple(do_invoke());
                return invoke_recurse<flags, Fmt, void, stage, next_arg>(std::move(args), seq, std::tuple_cat(std::move(result_so_far), std::move(result)));
            }
        }

        template<simd flags, simd_format Fmt, std::size_t stage, typename... A>
        auto invoke(std::tuple<A...>&& args)
        {
            constexpr auto slice = args_slice_size<flags, Fmt, stage, sizeof...(A)>(tuple_id<A...> { });
            constexpr auto seq = std::make_index_sequence<slice> { };
            if constexpr (stage + 1 < sizeof...(T))
            {
                auto result = invoke_slice<flags, Fmt, stage>(std::move(args), seq, std::tuple<> { });
                using Fmt2 = typename decltype(result)::format;
                return invoke<flags, Fmt2, stage + 1>(std::move(result.data));
            }
            else return invoke_slice<flags, Fmt, stage>(std::move(args), seq, std::tuple<> { });
        }

    public:
        template<typename... U>
        simd_pipeline(U&&... f) : stages { std::forward<U>(f)... } { }

        template<simd flags, simd_format Fmt, typename... A> requires (invocable<flags, Fmt>(tuple_id<A...> { }))
        [[gnu::flatten, gnu::hot]] auto operator()(Fmt, A&&... args)
        {
            return invoke<flags, Fmt, 0>(std::forward_as_tuple(std::forward<A>(args)...));
        }
    };

    template<typename... T> simd_pipeline(T...) -> simd_pipeline<std::remove_cvref_t<T>...>;

    // Execute a SIMD pipeline or single stage with the specified arguments,
    // trying simd_formats in the specified order.
    template<simd flags, simd_format Fmt, simd_format... Fmts, typename F, typename... A>
    [[gnu::flatten, gnu::hot]] auto simd_run(F&& func, A&&... args)
    {
        if constexpr (flags.match(simd_format_traits<Fmt>::flags) and simd_invocable<F, flags, Fmt, A...>)
            return simd_invoke<flags>(std::forward<F>(func), Fmt { }, std::forward<A>(args)...);
        else
        {
            static_assert (sizeof...(Fmts) > 0, "Unable to find suitable simd_format");
            return simd_run<flags, Fmts...>(std::forward<F>(func), std::forward<A>(args)...);
        }
    }

    // Execute a SIMD pipeline or single stage with the specified arguments,
    // using the default format search order.
    template<simd flags, typename F, typename... A>
    [[gnu::flatten, gnu::hot]] auto simd_run(F&& func, A&&... args)
    {
        return simd_run<flags, format_pi8, format_pi16, format_pi32, format_si64, format_ps, format_pf, format_nosimd>
            (std::forward<F>(func), std::forward<A>(args)...);
    }

    // Invoke a SIMD pipeline or single stage using the format and arguments
    // unpacked from simd_return data.
    template<simd flags, typename F, simd_return_type... A>
    [[gnu::flatten, gnu::hot]] auto simd_apply(F&& func, A&&... args)
    {
        constexpr auto get_format = []<typename B, typename... C>(std::type_identity<std::tuple<B, C...>>)
        {
            return typename B::format { };
        };
        using Fmt = decltype(get_format(std::type_identity<std::tuple<A...>> { }));
        static_assert ((std::same_as<Fmt, typename A::format> and ...), "Conflicting formats in arguments");

        auto invoke = [&func]<typename... A2>(A2&&... args)
        {
            return simd_run<flags, Fmt>(std::forward<F>(func), std::forward<A2>(args)...);
        };
        return std::apply(invoke, std::forward<A>(args).data...);
    }
}
