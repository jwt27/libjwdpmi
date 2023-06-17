#/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
#/*    Copyright (C) 2022 - 2023 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <tuple>
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

    template<typename... T>
    inline constexpr bool all_same = sizeof...(T) < 2 or (std::same_as<std::tuple_element_t<0, std::tuple<T...>>, T> and ...);

    template<typename T>
    concept simd_format = any_of<std::remove_cvref_t<T>, format_nosimd, format_pi8, format_pi16, format_pi32, format_si64, format_ps, format_pf>;

    template<typename T, typename... U>
    concept any_simd_format_of = simd_format<T> and (simd_format<U> and ...) and any_of<std::remove_cvref_t<T>, U...>;

    template<typename T, typename... U>
    concept any_simd_format_but = simd_format<T> and (simd_format<U> and ...) and not any_of<std::remove_cvref_t<T>, U...>;

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

    // Load simd_data from one or more iterators.  In addition to the stored
    // iterators, additional iterators may be passed by pointer to operator(),
    // and data from these is inserted first in the output stream.  All
    // iterators are incremented by the delta specified in simd_type_traits.
    template<typename... I>
    struct simd_source
    {
        template<typename... J>
        constexpr simd_source(J&&... i) : iterators { std::forward<J>(i)... } { }

        template<simd flags, simd_format F, typename... J> requires
            ((simd_loadable<I, flags, F> and ...) and
             (simd_loadable<J, flags, F> and ...))
        auto operator()(F, J*... it)
        {
            return load<flags>(F { }, std::make_index_sequence<sizeof...(I)> { }, it...);
        }

    private:
        template<simd flags, simd_format F, typename... J, std::size_t... N>
        auto load(F, std::index_sequence<N...>, J*... it)
        {
            auto ret = simd_return(F { }, simd_data<std::iter_value_t<J>>(simd_load<flags>(F { }, *it))...,
                                   simd_data<std::iter_value_t<std::tuple_element_t<N, std::tuple<I...>>>>(simd_load<flags>(F { }, std::get<N>(iterators)))...);
            (increment_simd_iterator<F>(it), ...);
            (increment_simd_iterator<F>(&std::get<N>(iterators)), ...);
            return ret;
        }

        std::tuple<I...> iterators;
    };

    template<typename... I> simd_source(I&&...) -> simd_source<std::decay_t<I>...>;

    // Store the incoming simd_data in a contained set of iterators.
    template<typename... I>
    struct simd_sink
    {
        template<typename... J>
        constexpr simd_sink(J&&... i) : iterators { std::forward<J>(i)... } { }

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

    template<typename... I> simd_sink(I&&...) -> simd_sink<std::decay_t<I>...>;

    // Convert input directly to SIMD data via simd_load.  This is only
    // possible for types where the returned SIMD vector represents one
    // element of T.  For regular arithmetic types, only format_nosimd
    // satisfies this constraint.
    struct simd_in_t
    {
        template<simd flags, simd_format F, typename T>
        requires (simd_loadable<const T*, flags, F> and simd_type_traits<T, F>::delta == 1)
        auto operator()(F, const T& value) const
        {
            return simd_data<T>(simd_load<flags>(F { }, &value));
        }
    } constexpr inline simd_in;

    // Convert SIMD data directly to output value via simd_store.  As with
    // simd_in, this only possible when the SIMD vector represents a single
    // element of the output type.
    struct simd_out_t
    {
        template<simd flags, simd_format F, simd_data_type D>
        requires (simd_storable<simd_type<D>*, flags, F> and simd_type_traits<simd_type<D>, F>::delta == 1
                  and std::is_default_constructible_v<simd_type<D>>)
        auto operator()(F, D data) const
        {
            simd_type<D> value;
            simd_store<flags>(F { }, &value, data);
            return value;
        }
    } constexpr inline simd_out;

    // Reinterpret simd_data as a different type.
    template<typename... T>
    struct simd_reinterpret
    {
        template<simd flags, typename... U> requires (sizeof...(T) == sizeof...(U))
            auto operator()(auto fmt, U&&... data) const
        {
            return simd_return(fmt, simd_data<T>(std::forward<U>(data))...);
        }
    };

    // Does nothing.
    struct simd_nop_t
    {
        template<simd flags, typename... T>
        auto operator()(auto fmt, T&&... data) const
        {
            return simd_return(fmt, std::forward<T>(data)...);
        }
    } constexpr inline simd_nop;
}

namespace jw::detail
{
    template<typename True, typename False>
    struct simd_if
    {
        template<simd flags, typename... T>
        auto operator()(auto fmt, T&&... data) const
        {
            if (condition)
                return simd_invoke<flags>(yes, fmt, std::forward<T>(data)...);
            else
                return simd_invoke<flags>(no, fmt, std::forward<T>(data)...);
        }

        const bool condition;
        True yes;
        False no;
    };

    template<bool Condition, typename True, typename False>
    struct simd_if_constexpr
    {
        template<simd flags, typename... T>
        auto operator()(auto fmt, T&&... data) const
        {
            if constexpr (Condition)
                return simd_invoke<flags>(yes, fmt, std::forward<T>(data)...);
            else
                return simd_invoke<flags>(no, fmt, std::forward<T>(data)...);
        }

        True yes;
        False no;
    };

    template<typename True, typename False, simd_format... Fmts>
    struct simd_if_format
    {
        template<simd flags, simd_format Fmt, typename... T>
        auto operator()(Fmt fmt, T&&... data) const
        {
            if constexpr (any_simd_format_of<Fmt, Fmts...>)
                return simd_invoke<flags>(yes, fmt, std::forward<T>(data)...);
            else
                return simd_invoke<flags>(no, fmt, std::forward<T>(data)...);
        }

        True yes;
        False no;
    };
}

namespace jw
{
    // Execute a simd_pipeline conditionally.
    template<typename True>
    auto simd_if(bool condition, True&& yes)
    {
        return detail::simd_if<True, simd_nop_t> { condition, std::forward<True>(yes), { } };
    }

    // Execute a simd_pipeline conditionally.
    template<typename True, typename False>
    auto simd_if(bool condition, True&& yes, False&& no)
    {
        return detail::simd_if<True, False> { condition, std::forward<True>(yes), std::forward<False>(no) };
    }

    // Execute a simd_pipeline conditionally.
    template<bool Condition, typename True>
    auto simd_if_constexpr(True&& yes)
    {
        return detail::simd_if_constexpr<Condition, True, simd_nop_t> { std::forward<True>(yes), { } };
    }

    // Execute a simd_pipeline conditionally.
    template<bool Condition, typename True, typename False>
    auto simd_if_constexpr(True&& yes, False&& no)
    {
        return detail::simd_if_constexpr<Condition, True, False> { std::forward<True>(yes), std::forward<False>(no) };
    }

    // Execute a simd_pipeline if the format matches any of those specified.
    template<simd_format... Fmts, typename True>
    auto simd_if_format(True&& yes)
    {
        return detail::simd_if_format<True, simd_nop_t, Fmts...> { std::forward<True>(yes), { } };
    }

    // Execute a simd_pipeline if the format matches any of those specified.
    template<simd_format... Fmts, typename True, typename False>
    auto simd_if_format(True&& yes, False&& no)
    {
        return detail::simd_if_format<True, False, Fmts...> { std::forward<True>(yes), std::forward<False>(no) };
    }

    template<std::size_t... I>
    struct simd_slice_t
    {
        template<simd, typename... T> requires (std::max({ I... }) < sizeof...(T))
        auto operator()(auto fmt, T&&... data) const
        {
            constexpr auto slice = [fmt](auto tuple) { return simd_return(fmt, std::get<I>(tuple)...); };
            return slice(std::tuple<T...> { std::forward<T>(data)... });
        }
    };

    // Selectively slice/shuffle/duplicate inputs.
    // Example: simd_slice<0, 0, 3, 2> (A, B, C, D, ...) -> (A, A, D, C)
    template<std::size_t... I>
    constexpr inline simd_slice_t<I...> simd_slice;

    template<std::size_t I, std::size_t N>
    struct simd_slice_sequential_t
    {
        template<simd, typename... T> requires (I + N <= sizeof...(T))
        auto operator()(auto fmt, T&&... data) const
        {
            constexpr auto slice = [fmt]<std::size_t... Is>(auto tuple, std::index_sequence<Is...>)
            {
                return simd_return(fmt, std::get<I + Is>(tuple)...);
            };
            return slice(std::forward_as_tuple(std::forward<T>(data)...), std::make_index_sequence<N> { });
        }
    };

    // Return the first N inputs.
    template<std::size_t N>
    constexpr inline simd_slice_sequential_t<0, N> simd_slice_first;

    // Return the first N inputs starting from index I.
    template<std::size_t I, std::size_t N>
    constexpr inline simd_slice_sequential_t<I, N> simd_slice_next;

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
                    static_assert (stage == sizeof...(T) - 1 or simd_data_type<result> or (sizeof...(N) == 1 and not std::same_as<result, void>),
                                   "Intermediate pipeline stages with multiple inputs must return via simd_data()");

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

        template<typename U>
        friend constexpr auto operator|(simd_pipeline&& pipe, U&& stage)
        {
            auto make = [&]<std::size_t... I>(std::index_sequence<I...>)
            {
                return simd_pipeline<T..., U> { std::get<I>(std::move(pipe).stages)..., std::forward<U>(stage) };
            };
            return make(std::index_sequence_for<T...> { });
        }

        template<simd flags, simd_format Fmt, typename... A> requires (invocable<flags, Fmt>(tuple_id<A...> { }))
        [[gnu::flatten, gnu::hot]] auto operator()(Fmt, A&&... args)
        {
            return invoke<flags, Fmt, 0>(std::forward_as_tuple(std::forward<A>(args)...));
        }
    };

    template<typename... T> simd_pipeline(T&&...) -> simd_pipeline<T...>;

    template<typename... I, typename T>
    constexpr auto operator| (simd_source<I...>&& src, T&& next)
    {
        return simd_pipeline { std::move(src), std::forward<T>(next) };
    }

    template<typename I, typename T> requires std::same_as<std::remove_cvref_t<I>, simd_in_t>
    constexpr auto operator| (I&& src, T&& next)
    {
        return simd_pipeline { std::forward<I>(src), std::forward<T>(next) };
    }

    // Execute a SIMD pipeline stage with the specified arguments, trying
    // simd_formats in the specified order.
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

    // Execute a SIMD pipeline with the specified arguments, using the default
    // format search order.
    template<simd flags, typename F, typename... A>
    [[gnu::flatten, gnu::hot]] auto simd_run(F&& func, A&&... args)
    {
        return simd_run<flags, format_pi8, format_pi16, format_pi32, format_si64, format_ps, format_pf, format_nosimd>
            (std::forward<F>(func), std::forward<A>(args)...);
    }

    // Invoke a SIMD pipeline using arguments unpacked from a tuple.
    template<simd flags, typename F, simd_format Fmt, typename Tuple>
    [[gnu::flatten, gnu::hot]] auto simd_apply(F&& func, Fmt, Tuple&& args)
    {
        auto invoke = [&func]<typename... A>(A&&... args)
        {
            return simd_invoke<flags>(std::forward<F>(func), Fmt { }, std::forward<A>(args)...);
        };
        return std::apply(invoke, std::forward<Tuple>(args));
    }

    // Invoke a SIMD pipeline using the format and arguments unpacked from
    // simd_return data.
    template<simd flags, typename F, simd_return_type A>
    [[gnu::flatten, gnu::hot]] auto simd_apply(F&& func, A&& args)
    {
        using Fmt = A::format;
        return simd_apply<flags>(std::forward<F>(func), Fmt { }, std::forward<A>(args).data);
    }
}

#include <jw/simd_load_store.h>
