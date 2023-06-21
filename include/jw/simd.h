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

    // Check if there is a valid overload of simd_load() for input iterator I
    // in the given format.
    template<typename I, simd flags, typename Fmt>
    concept simd_loadable = requires (Fmt t, I p)
    {
        { simd_load<flags>(t, p) } -> std::same_as<typename simd_type_traits<std::remove_cv_t<std::iter_value_t<I>>, Fmt>::data_type>;
    };

    // Check if there is a valid overload of simd_store() for output iterator
    // O in the given format.
    template<typename O, simd flags, typename Fmt>
    concept simd_storable = requires (Fmt t, O p, typename simd_type_traits<std::remove_cv_t<std::iter_value_t<O>>, Fmt>::data_type v)
    {
        simd_store<flags>(t, p, v);
    };
}

namespace jw::detail
{
    template<typename T, typename D>
    struct simd_data_t
    {
        using type = T;
        using data_type = D;
        const data_type data;

        constexpr operator data_type() const noexcept { return data; }
        constexpr const data_type& get() const noexcept { return data; }
    };

    template<typename>
    constexpr bool detect_simd_data = false;

    template<typename T, typename D>
    constexpr bool detect_simd_data<simd_data_t<T, D>> = true;
}

namespace jw
{
    // Check if the given type is a type produced by simd_data(), or a
    // reference to one.
    template<typename T>
    concept simd_data_type = detail::detect_simd_data<std::remove_cvref_t<T>>;

    // Read the type name stored by simd_data().
    template<simd_data_type D>
    using simd_type = typename std::remove_cvref_t<D>::type;

    // Check if the simd_type of D matches T.
    template<typename D, typename T>
    concept simd_data_for = simd_data_type<D> and std::same_as<simd_type<D>, T>;

    // Wrap data in a struct with the specified type info.
    template <typename T, typename D>
    auto simd_data(D&& data)
    {
        if constexpr (simd_data_type<std::decay_t<D>>)
            return simd_data<T, typename std::decay_t<D>::data_type>(std::forward<D>(data));
        else
            return detail::simd_data_t<std::remove_cvref_t<T>, std::decay_t<D>> { std::forward<D>(data) };
    }
}

namespace jw::detail
{
    template<simd_format F, typename T>
    struct simd_return_t
    {
        using format = F;
        using tuple = T;
        tuple data;
    };

    template<typename>
    constexpr bool detect_simd_return = false;

    template<simd_format F, typename T>
    constexpr bool detect_simd_return<simd_return_t<F, T>> = true;
}

namespace jw
{
    // Check if the given type is a type produced by simd_return(), or a
    // reference to one.
    template<typename T>
    concept simd_return_type = detail::detect_simd_return<std::remove_cvref_t<T>>;

    // Wrap simd_data in a struct with the specified format info.  All data is
    // converted to the data_type specified in simd_type_traits.
    template<simd_format F, simd_data_type... D>
    auto simd_return(F, D&&... data)
    {
        using tuple = std::tuple<detail::simd_data_t<simd_type<D>, typename simd_type_traits<simd_type<D>, F>::data_type>...>;
        auto convert = []<simd_data_type D2>(D2&& data)
        {
            using T = simd_type<D2>;
            using old_type = std::remove_cvref_t<D2>::data_type;
            using new_type = typename simd_type_traits<T, F>::data_type;
            if constexpr (not std::same_as<old_type, new_type>)
                return simd_data<T>(static_cast<new_type>(std::forward<D2>(data).data));
            else
                return data;
        };
        return detail::simd_return_t<F, tuple> { .data = { convert(std::forward<D>(data))... } };
    }

    // This placeholder type may be returned by SIMD pipeline stages to signal
    // that the given format and/or input types are not supported, as an
    // alternative to writing elaborate requires-expressions.
    class simd_invalid_t { } constexpr simd_invalid;

    // Check if a type is not simd_invalid_t.
    template<typename T>
    concept simd_valid = not std::same_as<std::remove_cvref_t<T>, simd_invalid_t>;

    // Check if functor object of type F is invocable with the specified flags
    // and arguments, and does not return simd_invalid.
    template<typename F, simd flags, typename... A>
    concept simd_invocable = requires(F func, A&&... args)
    {
        { (std::forward<F>(func).template operator()<flags>(std::forward<A>(args)...)) } -> simd_valid;
    };

    // Call a simd-invocable functor object with the specified flags and
    // arguments.
    template<simd flags, typename F, typename... A> requires simd_invocable<F&&, flags, A&&...>
    [[gnu::flatten]] decltype(auto) simd_invoke(F&& func, A&&... args)
    {
        return (std::forward<F>(func).template operator()<flags>(std::forward<A>(args)...));
    }

    // Execute a SIMD pipeline stage with the specified arguments, trying
    // simd_formats in the specified order.
    template<simd flags, simd_format Fmt, simd_format... Fmts, typename F, typename... A>
    [[gnu::flatten, gnu::hot]] auto simd_run(F&& func, A&&... args)
    {
        if constexpr (flags.match(simd_format_traits<Fmt>::flags) and simd_invocable<F&&, flags, Fmt&&, A&&...>)
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
    template<simd flags, typename F, simd_format Fmt, typename... A> requires (simd_invocable<F&&, flags, Fmt, A...>)
    [[gnu::flatten, gnu::hot]] auto simd_apply(F&& func, Fmt, std::tuple<A...>&& args)
    {
        auto invoke = [&func]<typename... A2>(A2&&... args)
        {
            return simd_invoke<flags>(std::forward<F>(func), Fmt { }, std::forward<A2>(args)...);
        };
        return std::apply(invoke, std::move(args));
    }

    // Invoke a SIMD pipeline using arguments unpacked from a tuple.
    template<simd flags, typename F, simd_format Fmt, typename... A> requires (simd_invocable<F&&, flags, Fmt, A...>)
    [[gnu::flatten, gnu::hot]] auto simd_apply(F&& func, Fmt, const std::tuple<A...>& args)
    {
        auto invoke = [&func]<typename... A2>(A2&&... args)
        {
            return simd_invoke<flags>(std::forward<F>(func), Fmt { }, std::forward<A2>(args)...);
        };
        return std::apply(invoke, args);
    }

    // Invoke a SIMD pipeline using the format and arguments unpacked from
    // simd_return data.
    template<simd flags, typename F, simd_return_type A>
    requires (requires (F&& func, A&& args) { simd_apply<flags>(std::forward<F>(func), typename A::format { }, std::forward<A>(args).data); })
    [[gnu::flatten, gnu::hot]] auto simd_apply(F&& func, A&& args)
    {
        using Fmt = A::format;
        return simd_apply<flags>(std::forward<F>(func), Fmt { }, std::forward<A>(args).data);
    }

    template<typename F, simd flags, typename... A>
    concept simd_applicable = requires (F&& func, A&&... args)
    {
        { simd_apply<flags>(std::forward<F>(func), std::forward<A>(args)...) } -> simd_valid;
    };
}

namespace jw::detail
{
    // Used to implement simd_invoke_result
    template<typename F, simd flags, typename... A>
    consteval auto find_simd_invoke_result()
    {
        if constexpr (simd_invocable<F, flags, A...>)
            return std::type_identity<decltype(simd_invoke<flags>(std::declval<F>(), std::declval<A>()...))> { };
        else
            return std::type_identity<simd_invalid_t> { };
    }

    // Used to implement simd_apply_result
    template<typename F, simd flags, typename... A>
    consteval auto find_simd_apply_result()
    {
        if constexpr (simd_applicable<F, flags, A...>)
            return std::type_identity<decltype(simd_apply<flags>(std::declval<F>(), std::declval<A>()...))> { };
        else
            return std::type_identity<simd_invalid_t> { };
    }
}

namespace jw
{
    // The result of calling a functor object of type F with the specified
    // flags and arguments.  The result is simd_invalid_t if F is not
    // simd-invocable.
    template<typename F, simd flags, typename... A>
    using simd_invoke_result = decltype(detail::find_simd_invoke_result<F, flags, A...>())::type;

    // The result of calling simd_apply() on a SIMD pipeline object of type F
    // with the specified flags and arguments.  The result is simd_invalid_t
    // if F is not simd-invocable.
    template<typename F, simd flags, typename... A>
    using simd_apply_result = decltype(detail::find_simd_apply_result<F, flags, A...>())::type;

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
        requires (simd_storable<I, flags, F> and ...)
        void operator()(F, D&&... data)
        {
            static_assert (sizeof...(D) == sizeof...(I));
            static_assert ((std::output_iterator<I, simd_type<D>> and ...));
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
    // and format_si64 satisfy this constraint.
    struct simd_in_t
    {
        template<simd flags, simd_format Fmt, typename... T>
        requires ((simd_loadable<const T*, flags, Fmt> and ...) and
                  ((simd_type_traits<T, Fmt>::delta == 1) and ...))
        auto operator()(Fmt, const T&... values) const
        {
            return simd_return(Fmt { }, simd_data<T>(simd_load<flags>(Fmt { }, &values))...);
        }
    } constexpr inline simd_in;

    // Convert SIMD data directly to output value via simd_store.  When this
    // SIMD data represents multiple elements of T, a std::array is returned.
    class simd_out_t
    {
        template<simd flags, simd_format Fmt, simd_data_type D>
        static auto store(Fmt, D data)
        {
            using T = simd_type<D>;
            constexpr auto N = simd_type_traits<T, Fmt>::delta;
            if constexpr (N == 1)
            {
                T value;
                simd_store<flags>(Fmt { }, &value, data);
                return value;
            }
            else
            {
                std::array<T, N> value;
                simd_store<flags>(Fmt { }, value.data(), data);
                return value;
            }
        }

    public:
        template<simd flags, simd_format Fmt, simd_data_type... D>
        requires (simd_storable<simd_type<D>*, flags, Fmt> and ...)
        auto operator()(Fmt, D&&... data) const
        {
            static_assert ((std::is_default_constructible_v<simd_type<D>> and ...));
            return std::make_tuple(store<flags>(Fmt {}, std::forward<D>(data))...);
        }
    } constexpr inline simd_out;

    // Reinterpret simd_data as a different type.
    template<typename... T>
    struct simd_reinterpret
    {
        template<simd flags, typename... U>
        auto operator()(auto fmt, U&&... data) const
        {
            static_assert(sizeof...(T) == sizeof...(U));
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

    template<typename True, typename False>
    struct simd_if_t
    {
        template<simd flags, simd_format Fmt, typename... A>
        requires (simd_invocable<True&, flags, Fmt&&, A&&...> and simd_invocable<False&, flags, Fmt&&, A&&...>)
        auto operator()(Fmt, A&&... args) const
        {
            if (condition)
                return simd_invoke<flags>(yes, Fmt { }, std::forward<A>(args)...);
            else
                return simd_invoke<flags>(no, Fmt { }, std::forward<A>(args)...);
        }

        const bool condition;
        True yes;
        False no;
    };

    // Execute a simd_pipeline conditionally.
    template<typename True>
    auto simd_if(bool condition, True&& yes)
    {
        return simd_if_t<True, simd_nop_t> { condition, std::forward<True>(yes), { } };
    }

    // Execute a simd_pipeline conditionally.
    template<typename True, typename False>
    auto simd_if(bool condition, True&& yes, False&& no)
    {
        return simd_if_t<True, False> { condition, std::forward<True>(yes), std::forward<False>(no) };
    }

    template<bool Condition, typename True, typename False>
    struct simd_if_constexpr_t
    {
        template<simd flags, simd_format Fmt, typename... A>
        requires (Condition ? simd_invocable<True&, flags, Fmt&&, A&&...> : simd_invocable<False&, flags, Fmt&&, A&&...>)
        auto operator()(Fmt, A&&... args) const
        {
            if constexpr (Condition)
                return simd_invoke<flags>(yes, Fmt { }, std::forward<A>(args)...);
            else
                return simd_invoke<flags>(no, Fmt { }, std::forward<A>(args)...);
        }

        True yes;
        False no;
    };

    // Execute a simd_pipeline conditionally.
    template<bool Condition, typename True>
    auto simd_if_constexpr(True&& yes)
    {
        return simd_if_constexpr_t<Condition, True, simd_nop_t> { std::forward<True>(yes), { } };
    }

    // Execute a simd_pipeline conditionally.
    template<bool Condition, typename True, typename False>
    auto simd_if_constexpr(True&& yes, False&& no)
    {
        return simd_if_constexpr_t<Condition, True, False> { std::forward<True>(yes), std::forward<False>(no) };
    }

    template<typename True, typename False, simd_format... Fmts>
    struct simd_if_format_t
    {
        template<simd flags, simd_format Fmt, typename... A>
        requires (any_simd_format_of<Fmt, Fmts...> and simd_invocable<True&, flags, Fmt&&, A&&...>)
        auto operator()(Fmt, A&&... args) const
        {
            return simd_invoke<flags>(yes, Fmt { }, std::forward<A>(args)...);
        }

        template<simd flags, simd_format Fmt, typename... A>
        requires (not any_simd_format_of<Fmt, Fmts...> and simd_invocable<False&, flags, Fmt&&, A&&...>)
        auto operator()(Fmt, A&&... args) const
        {
            return simd_invoke<flags>(no, Fmt { }, std::forward<A>(args)...);
        }

        True yes;
        False no;
    };

    // Execute a simd_pipeline if the format matches any of those specified.
    template<simd_format... Fmts, typename True>
    auto simd_if_format(True&& yes)
    {
        return simd_if_format_t<True, simd_nop_t, Fmts...> { std::forward<True>(yes), { } };
    }

    // Execute a simd_pipeline if the format matches any of those specified.
    template<simd_format... Fmts, typename True, typename False>
    auto simd_if_format(True&& yes, False&& no)
    {
        return simd_if_format_t<True, False, Fmts...> { std::forward<True>(yes), std::forward<False>(no) };
    }

    template<std::size_t... I>
    struct simd_slice_t
    {
        template<simd, simd_format Fmt, typename... T>
        auto operator()(Fmt, T&&... data) const
        {
            static_assert (std::max({ I... }) < sizeof...(T));
            constexpr auto slice = [](auto tuple)
            {
                return simd_return(Fmt { }, std::get<I>(tuple)...);
            };
            return slice(std::tuple<const T&...> { std::forward<T>(data)... });
        }
    };

    // Selectively slice/shuffle/duplicate inputs.
    // Example: simd_slice<0, 0, 3, 2> (A, B, C, D, ...) -> (A, A, D, C)
    template<std::size_t... I>
    constexpr inline simd_slice_t<I...> simd_slice;

    template<std::size_t I, std::size_t N>
    struct simd_slice_sequential_t
    {
        template<simd, simd_format Fmt, typename... T>
        auto operator()(Fmt, T&&... data) const
        {
            static_assert (I + N <= sizeof...(T));
            constexpr auto slice = []<std::size_t... Is>(auto&& tuple, std::index_sequence<Is...>)
            {
                return simd_return(Fmt { }, std::get<I + Is>(std::move(tuple))...);
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
}

namespace jw::detail
{
    template<typename>
    constexpr bool is_tuple = false;

    template<typename... T>
    constexpr bool is_tuple<std::tuple<T...>> = true;

    template<std::size_t I, typename... A>
    using pack_element = std::tuple_element_t<I, std::tuple<A...>>;

    template<simd_return_type... R>
    constexpr auto simd_return_cat(R&&... result)
    {
        using format = typename pack_element<0, R...>::format;
        static_assert ((std::same_as<format, typename R::format> and ...),
                       "Inconsistent return formats from SIMD pipeline stage.");
        auto make = []<typename T, std::size_t... Is>(T&& tuple, std::index_sequence<Is...>)
        {
            return simd_return(format { }, std::get<Is>(std::move(tuple))...);
        };
        return make(std::tuple_cat(std::forward<R>(result).data...), std::make_index_sequence<sizeof...(R)> { });
    }

    template<typename... R>
    constexpr auto simd_loop_result(std::tuple<R...>&& result)
    {
        static_assert((simd_valid<R> and ...));

        if constexpr (sizeof...(R) == 0)
            return std::make_tuple();
        else
        {
            auto make = []<std::size_t... Is>(std::index_sequence<Is...>, auto&& result)
            {
                if constexpr ((simd_return_type<R> or ...))
                {
                    static_assert ((simd_return_type<R> and ...),
                                   "Inconsistent return types from SIMD pipeline stage.");
                    return simd_return_cat(std::get<Is>(std::move(result))...);
                }
                else return std::tuple_cat(std::get<Is>(std::move(result))...);
            };
            return make(std::make_index_sequence<sizeof...(R)> { }, std::move(result));
        }
    }

    // Wraps output from a pipeline stage in simd_return(simd_data(...)).
    // Tuples are passed through unchanged.  A void return type produces an
    // empty tuple.
    template<typename T>
    struct simd_pipeline_wrapper
    {
        T pipe;

        template<simd flags, simd_format Fmt, typename... A> requires simd_invocable<T, flags, Fmt, A&&...>
        auto operator()(Fmt, A&&... args)
        {
            using R = simd_invoke_result<T, flags, Fmt, A&&...>;
            auto invoke = [&] { return simd_invoke<flags>(pipe, Fmt { }, std::forward<A>(args)...); };

            if constexpr (simd_return_type<R> or is_tuple<R>)
                return invoke();
            else if constexpr (simd_data_type<R>)
                return simd_return(Fmt { }, invoke());
            else if constexpr (std::is_void_v<R>)
            {
                invoke();
                return std::make_tuple();
            }
            else
            {
                static_assert(sizeof...(A) == 1, "Pipeline stages with multiple inputs must return via simd_data().");
                using U = simd_type<std::tuple_element_t<0, std::tuple<A...>>>;
                return simd_return(Fmt { }, simd_data<U>(invoke()));
            }
        }
    };

    template<typename T>
    struct simd_loop_base
    {
        using pipe_t = simd_pipeline_wrapper<T>;
        pipe_t pipe;

        template<std::size_t slice_size, std::size_t loop_count, typename... A>
        static consteval auto find_slice_size()
        {
            if constexpr (slice_size > 0) return slice_size;
            else return sizeof...(A) / loop_count;
        }

        template<simd flags, simd_format Fmt, std::size_t I, typename... A, std::size_t... Is>
        static consteval bool invocable_slice(std::index_sequence<Is...> seq)
        {
            const auto next = I + sizeof...(Is);
            if constexpr (not simd_invocable<pipe_t&, flags, Fmt, pack_element<I + Is, A...>...>)
                return false;
            else if constexpr (next != sizeof...(A))
                return invocable_slice<flags, Fmt, next, A...>(seq);
            else
                return true;
        }

        template<simd flags, std::size_t slice_size, std::size_t loop_count, simd_format Fmt, typename... A>
        static consteval bool invocable()
        {
            static_assert (slice_size == 0 or loop_count == 0);
            static_assert (not (slice_size > 0 and sizeof...(A) == 0));

            if constexpr (sizeof...(A) == 0)
                return simd_invocable<pipe_t&, flags, Fmt>;
            else
            {
                static_assert (sizeof...(A) % std::max(slice_size, loop_count) == 0,
                               "Input count not divisible by slice size.");

                constexpr auto N = find_slice_size<slice_size, loop_count, A...>();
                return invocable_slice<flags, Fmt, 0, A...>(std::make_index_sequence<N> { });
            }
        }

        template<simd flags, simd_format Fmt, std::size_t I = 0, std::size_t... Is, typename... A, typename... R>
        auto invoke_slice(std::index_sequence<Is...> seq, std::tuple<A...>&& args, std::tuple<R...>&& result = std::tuple<> { })
        {
            constexpr auto next = I + sizeof...(Is);
            auto new_result = std::tuple_cat(std::move(result), std::make_tuple(simd_invoke<flags>(pipe, Fmt { }, std::get<I + Is>(std::move(args))...)));
            if constexpr (next != sizeof...(A))
                return invoke_slice<flags, Fmt, next>(seq, std::move(args), std::move(new_result));
            else
                return simd_loop_result(std::move(new_result));
        }

        template<simd flags, simd_format Fmt, std::size_t N, std::size_t I = 0, typename... R>
        auto invoke_loop(std::tuple<R...>&& result = std::tuple<> { })
        {
            auto new_result = std::tuple_cat(std::move(result), std::make_tuple(simd_invoke<flags>(pipe, Fmt { })));
            if constexpr (I < N)
                return invoke_loop<flags, Fmt, N, I + 1>(std::move(new_result));
            else
                return simd_loop_result(std::move(new_result));
        }

        template<simd flags, std::size_t slice_size, std::size_t loop_count, simd_format Fmt, typename... A>
        requires (invocable<flags, slice_size, loop_count, Fmt, A&&...>())
        auto invoke(Fmt, A&&... args)
        {
            if constexpr (sizeof...(A) == 0)
                return invoke_loop<flags, Fmt, loop_count>();
            else
            {
                constexpr auto N = find_slice_size<slice_size, loop_count, A...>();
                return invoke_slice<flags, Fmt>(std::make_index_sequence<N> { }, std::forward_as_tuple(std::forward<A>(args)...));
            }
        }
    };
}

namespace jw
{
    template<std::size_t N, typename T>
    struct simd_repeat_t : private detail::simd_loop_base<T>
    {
        using base = detail::simd_loop_base<T>;

        template<typename... U>
        simd_repeat_t(U&&... args) : base { std::forward<U>(args)... } { }

        template<simd flags, simd_format Fmt, typename... A>
        requires (base::template invocable<flags, 0, N, Fmt&&, A&&...>())
        auto operator()(Fmt, A&&... args)
        {
            return base::template invoke<flags, 0, N>(Fmt { }, std::forward<A>(args)...);
        }
    };

    // Execute a SIMD pipeline N times.  If there are any inputs, they are
    // sliced accordingly.
    template<std::size_t N, typename T>
    auto simd_repeat(T&& pipe)
    {
        return simd_repeat_t<N, T> { std::forward<T>(pipe) };
    }

    template<std::size_t N, typename T>
    struct simd_foreach_t : private detail::simd_loop_base<T>
    {
        using base = detail::simd_loop_base<T>;

        template<typename... U>
        simd_foreach_t(U&&... args) : base { std::forward<U>(args)... } { }

        template<simd flags, simd_format Fmt, typename... A>
        requires (base::template invocable<flags, N, 0, Fmt, A...>())
        auto operator()(Fmt, A&&... args)
        {
            return base::template invoke<flags, N, 0>(Fmt { }, std::forward<A>(args)...);
        }
    };

    // Execute a SIMD pipeline multiple times, once for every N inputs.
    template<std::size_t N = 1, typename T>
    auto simd_foreach(T&& pipe)
    {
        return simd_foreach_t<N, T> { std::forward<T>(pipe) };
    }

    // Execute multiple independent SIMD pipelines.  Each receives the same
    // inputs.
    template<typename... T>
    class simd_parallel
    {
        std::tuple<detail::simd_pipeline_wrapper<T>...> pipes;

        template<simd flags, simd_format Fmt, typename... A, std::size_t... I>
        auto invoke(std::index_sequence<I...>, Fmt, const A&... args)
        {
            return detail::simd_loop_result(std::forward_as_tuple(simd_invoke<flags>(std::get<I>(pipes), Fmt { }, args...)...));
        }

    public:
        template<typename... U>
        simd_parallel(U&&... args) : pipes { std::forward<U>(args)... } { }

        template<simd flags, simd_format Fmt, typename... A>
        requires (simd_invocable<detail::simd_pipeline_wrapper<T>&, flags, Fmt&&, const A&...> and ...)
        auto operator()(Fmt, A&&... args)
        {
            return invoke<flags>(std::make_index_sequence<sizeof...(T)> { }, Fmt { }, std::forward<A>(args)...);
        }
    };

    template<typename... T>
    simd_parallel(T&&...) -> simd_parallel<T...>;

    // A SIMD pipeline represents a sequence of functor objects, each of which
    // defines an operator() with the following signature:
    //   template<simd flags> auto operator()(simd_format auto fmt, auto... src)
    // The input data is wrapped by simd_data() so that it encodes the type of
    // data that is being operated on.  This can be recovered via simd_type.
    // Data is then returned via simd_return(fmt, simd_data<T>(dst)...), which
    // is passed on to the next stage.  If a stage only accepts a single
    // input, and does not change its format or type, the result may be
    // returned directly.  Arbitrary types may be returned via std::tuple.
    template<typename... T>
    class simd_pipeline
    {
        std::tuple<detail::simd_pipeline_wrapper<T>...> stages;

        template<std::size_t I>
        using stage_t = std::tuple_element_t<I, std::tuple<detail::simd_pipeline_wrapper<T>...>>;

        template<simd flags, typename A, std::size_t I>
        static consteval bool applicable()
        {
            if constexpr (I == sizeof...(T))
                return true;
            else
            {
                using result = simd_apply_result<stage_t<I>&, flags, A>;
                if constexpr (not simd_valid<result>)
                    return false;
                else if constexpr (simd_return_type<result>)
                    return applicable<flags, result&&, I + 1>();
                else
                    return true;    // Handled by simd_run()
            }
        }

        template<simd flags, simd_format Fmt, typename... A>
        static consteval bool invocable()
        {
            if constexpr (sizeof...(T) == 0)
                return false;
            else
            {
                using result = simd_invoke_result<stage_t<0>&, flags, Fmt&&, A&&...>;
                if constexpr (not simd_valid<result>)
                    return false;
                else
                    return applicable<flags, result&&, 1>();
            }
        }

        template<simd flags, std::size_t I, typename A>
        auto apply(A&& args)
        {
            if constexpr (I == sizeof...(T))
                return args;
            else if constexpr (simd_return_type<A>)
                return apply<flags, I + 1>(simd_apply<flags>(std::get<I>(stages), std::forward<A>(args)));
            else
            {
                // Must be a tuple, call simd_run() to find a new format.
                auto apply_run = [this]<typename... A2>(A2&&... args)
                {
                    return simd_run<flags>(std::get<I>(stages), std::forward<A2>(args)...);
                };
                return apply<flags, I + 1>(std::apply(apply_run, std::forward<A>(args)));
            }
        }

    public:
        template<typename... U>
        simd_pipeline(U&&... args) : stages { std::forward<U>(args)... } { }

        template<typename U>
        friend constexpr auto operator|(simd_pipeline&& pipe, U&& stage)
        {
            auto make = [&]<std::size_t... I>(std::index_sequence<I...>)
            {
                return simd_pipeline<T..., U> { std::get<I>(std::move(pipe).stages)..., std::forward<U>(stage) };
            };
            return make(std::index_sequence_for<T...> { });
        }

        template<simd flags, simd_format Fmt, typename... A>
        requires (invocable<flags, Fmt, A&&...>())
        [[gnu::flatten, gnu::hot]] auto operator()(Fmt, A&&... args)
        {
            return apply<flags, 1>(simd_invoke<flags>(std::get<0>(stages), Fmt { }, std::forward<A>(args)...));
        }

        template<simd flags, typename... A>
        [[gnu::flatten, gnu::hot]] auto run(A&&... args)
        {
            return simd_run<flags>(*this, std::forward<A>(args)...);
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
}

#include <jw/simd_load_store.h>
