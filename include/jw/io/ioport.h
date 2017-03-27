/******************************* libjwdpmi **********************************
Copyright (C) 2016-2017  J.W. Jagersma

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once
#include <cstdint>
#include <jw/common.h>

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

        template <typename T = byte>
        struct out_port
        {
            void write(T value) const { detail::out<T>(p, value); }
            auto& operator=(auto value) { write(value); return *this; }

            constexpr out_port(auto _p) noexcept : p(_p) { }
            out_port(const out_port&) = delete;
            out_port& operator=(const out_port&) = delete;
        private:
            const port_num p;
        };

        template <typename T = byte>
        struct in_port
        {
            auto read() const { return detail::in<T>(p); }
            operator T() { return read(); }

            constexpr in_port(auto _p) noexcept : p(_p) { }
            in_port(const in_port&) = delete;
            in_port& operator=(const in_port&) = delete;
        private:
            const port_num p;
        };

        template <typename T = byte>
        struct io_port final : public in_port<T>, public out_port<T>
        {
            constexpr io_port(auto _p) noexcept : in_port<T>(_p), out_port<T>(_p) { }
        };
    }
}
