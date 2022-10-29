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
    template<typename T, simd_format F> requires (std::is_arithmetic_v<T>)
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

    template<typename I, typename Fmt> concept can_load = requires (Fmt t, I p) { { simd_load(t, p) } -> std::same_as<typename simd_type_traits<std::remove_cv_t<std::iter_value_t<I>>, Fmt>::data_type>; };
    template<typename O, typename Fmt> concept can_store = requires (Fmt t, O p, typename simd_type_traits<std::remove_cv_t<std::iter_value_t<O>>, Fmt>::data_type v) { simd_store(t, p, v); };

    template<std::indirectly_readable I> requires (std::is_arithmetic_v<std::iter_value_t<I>>)
    [[gnu::always_inline]] inline auto simd_load(format_nosimd, I src)
    {
        return *src;
    }

    template<typename T, std::indirectly_writable<T> O> requires (std::is_arithmetic_v<std::iter_value_t<O>>)
    [[gnu::always_inline]] inline void simd_store(format_nosimd, O dst, T src)
    {
        *dst = src;
    }

    template<std::integral T> requires (sizeof(T) == 1)
    [[gnu::always_inline]] inline __m64 simd_load(format_pi8, const T* src)
    {
        return *reinterpret_cast<const __m64*>(src);
    }

    template<std::integral T> requires (sizeof(T) == 1)
    [[gnu::always_inline]] inline void simd_store(format_pi8, T* dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(dst) = src;
    }

    template<std::integral T> requires (sizeof(T) <= 2)
    [[gnu::always_inline]] inline __m64 simd_load(format_pi16, const T* src)
    {
        if constexpr (sizeof(T) == 2) return *reinterpret_cast<const __m64*>(src);
        else
        {
            __m64 data = simd_load(pi8, src);
            __m64 sign = _mm_setzero_si64();
            if constexpr (std::is_signed_v<T>) sign = _mm_cmpgt_pi8(sign, data);
            data = _mm_unpacklo_pi8(data, sign);
            return data;
        }
    }

    template<std::integral T> requires (sizeof(T) == 2)
    [[gnu::always_inline]] inline void simd_store(format_pi16, T* dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(dst) = src;
    }

    template<std::integral T> requires (sizeof(T) == 1)
    [[gnu::always_inline]] inline void simd_store(format_pi16, T* dst, __m64 src)
    {
        const __m64 a = std::is_signed_v<T> ? _mm_packs_pi16(src, src) : _mm_packs_pu16(src, src);
        *reinterpret_cast<std::uint32_t*>(dst) = _mm_cvtsi64_si32(a);
    }

    template<std::integral T> requires (sizeof(T) <= 4)
    [[gnu::always_inline]] inline __m64 simd_load(format_pi32, const T* src)
    {
        if constexpr (sizeof(T) == 4) return *reinterpret_cast<const __m64*>(src);
        else
        {
            __m64 data = simd_load(pi16, src);
            __m64 sign = _mm_setzero_si64();
            if constexpr (std::is_signed_v<T>) sign = _mm_cmpgt_pi16(sign, data);
            data = _mm_unpacklo_pi16(data, sign);
            return data;
        }
    }

    template<std::integral T> requires (sizeof(T) == 4)
    [[gnu::always_inline]] inline void simd_store(format_pi32, T* dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(dst) = src;
    }

    template<std::signed_integral T> requires (sizeof(T) == 2)
    [[gnu::always_inline]] inline void simd_store(format_pi32, T* dst, __m64 src)
    {
        const __m64 a = _mm_packs_pi32(src, src);
        *reinterpret_cast<std::uint32_t*>(dst) = _mm_cvtsi64_si32(a);
    }

    template<std::integral T> requires (sizeof(T) == 1)
    [[gnu::always_inline]] inline void simd_store(format_pi32, T* dst, __m64 src)
    {
        __m64 data = _mm_packs_pi32(src, src);
        data = std::is_signed_v<T> ? _mm_packs_pi16(data, data) : _mm_packs_pu16(data, data);
        *reinterpret_cast<std::uint16_t*>(dst) = _mm_cvtsi64_si32(data);
    }

    template<std::integral T> requires (sizeof(T) <= 8)
    [[gnu::always_inline]] inline __m64 simd_load(format_si64, const T* src)
    {
        if constexpr (sizeof(T) == 8) return *reinterpret_cast<const __m64*>(src);
        else
        {
            __m64 data = simd_load(pi32, src);
            __m64 sign = _mm_setzero_si64();
            if constexpr (std::is_signed_v<T>) sign = _mm_cmpgt_pi32(sign, data);
            data = _mm_unpacklo_pi32(data, sign);
            return data;
        }
    }

    template<std::integral T> requires (sizeof(T) == 8)
    [[gnu::always_inline]] inline void simd_store(format_si64, T* dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(dst) = src;
    }

    [[gnu::always_inline]] inline __m128 simd_load(format_ps, const float* src)
    {
        return *reinterpret_cast<const __m128*>(src);
    }

    template<std::signed_integral T> requires (sizeof(T) == 4)
    [[gnu::always_inline]] inline __m128 simd_load(format_ps, const T* src)
    {
        const __m64 lo = *reinterpret_cast<const __m64*>(src + 0);
        const __m64 hi = *reinterpret_cast<const __m64*>(src + 2);
        return _mm_cvtpi32x2_ps(lo, hi);
    }

    template<std::integral T> requires (sizeof(T) <= 2)
    [[gnu::always_inline]] inline __m128 simd_load(format_ps, const T* src)
    {
        const __m64 data = simd_load(pi16, src);
        return std::is_signed_v<T> ? _mm_cvtpi16_ps(data) : _mm_cvtpu16_ps(data);
    }

    [[gnu::always_inline]] inline void simd_store(format_ps, float* dst, __m128 src)
    {
        *reinterpret_cast<__m128*>(dst) = src;
    }

    template<std::integral T> requires (sizeof(T) <= 4 and (std::is_signed_v<T> or sizeof(T) == 1))
    [[gnu::always_inline]] inline void simd_store(format_ps, T* dst, __m128 src)
    {
        const __m64 lo = _mm_cvtps_pi32(src);
        const __m64 hi = _mm_cvtps_pi32(_mm_movehl_ps(src, src));
        if constexpr (can_store<T, format_pi16>)
            simd_store(pi16, dst, _mm_packs_pi32(lo, hi));
        else
        {
            simd_store(pi32, dst + 0, lo);
            simd_store(pi32, dst + 2, hi);
        }
    }

    [[gnu::always_inline]] inline __m64 simd_load(format_pf, const float* src)
    {
        return *reinterpret_cast<const __m64*>(src);
    }

    template<std::integral T> requires (sizeof(T) <= 4 and (std::is_signed_v<T> or sizeof(T) <= 2))
    [[gnu::always_inline]] inline __m64 simd_load(format_pf, const T* src)
    {
        return _m_pi2fd(simd_load(pi32, src));
    }

    [[gnu::always_inline]] inline void simd_store(format_pf, float* dst, __m64 src)
    {
        *reinterpret_cast<__m64*>(dst) = src;
    }

    template<std::integral T> requires (sizeof(T) <= 4 and (std::is_signed_v<T> or sizeof(T) == 1))
    [[gnu::always_inline]] inline void simd_store(format_pf, T* dst, __m64 src)
    {
        simd_store(pi32, dst, _m_pf2id(src));
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
            using data_type = D;
            const data_type data;

            constexpr operator data_type() const noexcept { return data; }
        };
        if constexpr (requires { typename std::decay_t<D>::data_type; })
            return simd_data<T, typename std::decay_t<D>::data_type>(std::forward<D>(data));
        else
            return impl { std::forward<D>(data) };
    }

    // Read the type name stored by simd_data().
    template<typename D> using simd_type = typename std::decay_t<D>::type;

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
        return make(simd_data<simd_type<D>>(std::forward<typename simd_type_traits<simd_type<D>, F>::data_type>(data))...);
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
        template<simd flags, simd_format F, typename... I> requires (can_load<I, F> and ...)
        auto operator()(F, I*... iterators)
        {
            auto ret = simd_return(F { }, simd_data<std::iter_value_t<I>>(simd_load(F { }, *iterators))...);
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
                  (can_store<I, F> and ...) and
                  (std::output_iterator<I, simd_type<D>> and ...))
        void operator()(F, D&&... data)
        {
            store(F { }, std::index_sequence_for<D...> { }, std::forward_as_tuple(std::forward<D>(data)...));
        }

    private:
        template<simd_format F, std::size_t... N>
        void store(F, std::index_sequence<N...>, auto data)
        {
            (simd_store(F { }, std::get<N>(iterators), std::get<N>(data)), ...);
            (increment_simd_iterator<F>(&std::get<N>(iterators)), ...);
        }

        std::tuple<I...> iterators;
    };

    // A SIMD pipeline is composed of one or more functor objects, each of
    // which defines an operator() with the following signature:
    //  template<simd flags> auto operator()(simd_format fmt, auto... src)
    // The input data is wrapped by simd_data() so that it encodes the type of
    // data that is being operated on.  This can be recovered via simd_type.
    // Data is then returned via simd_return(fmt, simd_data<T>(dst)...), which
    // is passed on to the next stage.  Only the first and last stages may
    // accept/return arbitrary types.
    // When a stage produces more data than the next stage can accept, that
    // next stage is invoked multiple times.
    template<typename... T>
    class simd_pipeline
    {
        std::tuple<T...> stages;

        template<typename... U> using tuple_id = std::type_identity<std::tuple<U...>>;

        template<typename U> static constexpr bool is_simd_return = requires (U r)
        {
            typename U::format;
            requires simd_format<typename U::format>;
            { r.data };
        };

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
                using result_t = typename result_id::type;
                if constexpr (is_simd_return<result_t>)
                {
                    using r_format = typename result_t::format;
                    using r_data = decltype(result_t::data);
                    using cumulative_data = decltype(std::tuple_cat(std::declval<std::tuple<R...>>(), std::declval<r_data>()));
                    static_assert (std::same_as<RFmt, void> or std::same_as<RFmt, r_format>, "Pipeline stage returns conflicting formats");
                    return invocable_recurse<flags, Fmt, stage, next_arg, r_format>(std::type_identity<cumulative_data> { }, args, seq);
                }
                else
                {
                    static_assert (stage == sizeof...(T) - 1, "Intermediate pipeline stages must return via simd_return()");
                    if constexpr (not std::same_as<result_t, void>)
                        return invocable_recurse<flags, Fmt, stage, next_arg, void>(tuple_id<R..., result_t> { }, args, seq);
                    else
                        return invocable_recurse<flags, Fmt, stage, next_arg, void>(tuple_id<> { }, args, seq);
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
        auto invoke_recurse(std::tuple<A&&...>&& args, auto seq, R&& result_so_far)
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
        auto invoke_slice(std::tuple<A&&...>&& args, std::index_sequence<N...> seq, std::tuple<R...> result_so_far)
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
            else if constexpr (is_simd_return<typename result_id::type>)
            {
                auto result = do_invoke();
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
        auto invoke(std::tuple<A&&...>&& args)
        {
            constexpr auto slice = args_slice_size<flags, Fmt, stage, sizeof...(A)>(tuple_id<A...> { });
            constexpr auto seq = std::make_index_sequence<slice> { };
            if constexpr (stage + 1 < sizeof...(T))
            {
                auto result = invoke_slice<flags, Fmt, stage>(std::move(args), seq, std::tuple<> { });
                using Fmt2 = typename decltype(result)::format;
                return std::apply([this]<typename... A2>(A2&&... args) { return invoke<flags, Fmt2, stage + 1>(std::forward_as_tuple(std::forward<A2>(args)...)); }, std::move(result.data));
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

    // Execute a SIMD pipeline or single stage with the specified arguments.
    template<simd flags, typename F, typename... A>
    [[gnu::flatten, gnu::hot]] auto simd_run(F&& func, A&&... args)
    {
        constexpr auto can_invoke = []<typename Fmt>(Fmt) consteval
        {
            return flags.match(simd_format_traits<Fmt>::flags) and simd_invocable<F, flags, Fmt, A...>;
        };

        auto do_invoke = [&]<typename Fmt>(Fmt)
        {
            return simd_invoke<flags>(std::forward<F>(func), Fmt { }, std::forward<A>(args)...);
        };

        if constexpr (can_invoke(pi8)) return do_invoke(pi8);
        else if constexpr (can_invoke(pi16)) return do_invoke(pi16);
        else if constexpr (can_invoke(pi32)) return do_invoke(pi32);
        else if constexpr (can_invoke(si64)) return do_invoke(si64);
        else if constexpr (can_invoke(ps)) return do_invoke(ps);
        else if constexpr (can_invoke(pf)) return do_invoke(pf);
        else return do_invoke(nosimd);
    }
}
