/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstdint>
#include <type_traits>

namespace jw
{
    namespace io
    {
        using port_num = std::uint_fast16_t;
        namespace detail
        {
        #define PORT_OUT(reg)       \
            asm volatile(           \
                "out %w1, "#reg";"  \
                :: "a" (v)          \
                , "Nd" (p));

        #define PORT_IN(reg)        \
            T v;                    \
            asm volatile(           \
                "in "#reg", %w1;"   \
                : "=a" (v)          \
                : "Nd" (p));        \
            return v;

        #define PORT_OUT_NONTRIVIAL(temp_type, reg) \
            asm volatile(                           \
                "out %w1, "#reg";"                  \
                :: "a" (static_cast<temp_type>(v))  \
                , "Nd" (p));

        #define PORT_IN_NONTRIVIAL(temp_type, reg)  \
            temp_type v;                            \
            asm volatile(                           \
                "in "#reg", %w1;"                   \
                : "=a" (v)                          \
                : "Nd" (p));                        \
            return T { v };

            template<typename T, std::size_t size> using enable_if_trivial_and_sizeof_eq = std::enable_if_t<sizeof(T) == size && std::is_trivially_default_constructible<T>::value, int>;
            template<typename T, std::size_t size> using enable_if_nontrivial_and_sizeof_eq = std::enable_if_t<sizeof(T) == size && !std::is_trivially_default_constructible<T>::value, int>;

            template <typename T, enable_if_trivial_and_sizeof_eq<T, 1> = { } > inline void out(port_num p, T v) noexcept { PORT_OUT(al); }
            template <typename T, enable_if_trivial_and_sizeof_eq<T, 2> = { } > inline void out(port_num p, T v) noexcept { PORT_OUT(ax); }
            template <typename T, enable_if_trivial_and_sizeof_eq<T, 4> = { } > inline void out(port_num p, T v) noexcept { PORT_OUT(eax); }
            template <typename T, enable_if_trivial_and_sizeof_eq<T, 1> = { } > inline auto in(port_num p) noexcept { PORT_IN(al); }
            template <typename T, enable_if_trivial_and_sizeof_eq<T, 2> = { } > inline auto in(port_num p) noexcept { PORT_IN(ax); }
            template <typename T, enable_if_trivial_and_sizeof_eq<T, 4> = { } > inline auto in(port_num p) noexcept { PORT_IN(eax); }

            template <typename T, enable_if_nontrivial_and_sizeof_eq<T, 1> = { } > inline void out(port_num p, T v) { PORT_OUT_NONTRIVIAL(std::uint8_t, al); }
            template <typename T, enable_if_nontrivial_and_sizeof_eq<T, 2> = { } > inline void out(port_num p, T v) { PORT_OUT_NONTRIVIAL(std::uint16_t, ax); }
            template <typename T, enable_if_nontrivial_and_sizeof_eq<T, 4> = { } > inline void out(port_num p, T v) { PORT_OUT_NONTRIVIAL(std::uint32_t, eax); }
            template <typename T, enable_if_nontrivial_and_sizeof_eq<T, 1> = { } > inline auto in(port_num p) { PORT_IN_NONTRIVIAL(std::uint8_t, al); }
            template <typename T, enable_if_nontrivial_and_sizeof_eq<T, 2> = { } > inline auto in(port_num p) { PORT_IN_NONTRIVIAL(std::uint16_t, ax); }
            template <typename T, enable_if_nontrivial_and_sizeof_eq<T, 4> = { } > inline auto in(port_num p) { PORT_IN_NONTRIVIAL(std::uint32_t, eax); }

        #undef PORT_OUT
        #undef PORT_IN
        #undef PORT_OUT_NONTRIVIAL
        #undef PORT_IN_NONTRIVIAL
        }

        template <typename T = std::byte>
        struct out_port
        {
            void write(T value) const { detail::out<T>(port, value); }
            void operator()(T value) const { return write(value); }

            constexpr out_port(port_num p) noexcept : port { p } { }
            out_port(const out_port&) = delete;
            out_port& operator=(const out_port&) = delete;

            const port_num port;
        };

        template <typename T = std::byte>
        struct in_port
        {
            auto read() const { return detail::in<T>(port); }
            T operator()() const { return read(); }

            constexpr in_port(port_num p) noexcept : port { p } { }
            in_port(const in_port&) = delete;
            in_port& operator=(const in_port&) = delete;

            const port_num port;
        };

        template <typename T = std::byte>
        struct io_port final : public in_port<T>, public out_port<T>
        {
            constexpr io_port(port_num p) noexcept : in_port<T> { p }, out_port<T> { p } { }
        };
    }
}
