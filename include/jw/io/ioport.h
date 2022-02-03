/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstdint>
#include <type_traits>

namespace jw::io
{
    using port_num = std::uint_fast16_t;

    template<typename T = std::byte> requires (std::is_trivial_v<T>)
    inline T read_port(port_num p) { T v; asm volatile ("in %0, %w1" : "=a" (v) : "Nd" (p)); return v; }

    template<typename T> requires (std::is_trivial_v<T>)
    inline void write_port(port_num p, const T& v) noexcept { asm volatile ("out %w0, %1" :: "Nd" (p), "a" (v)); }

    template <typename T = std::byte>
    struct in_port
    {
        T read() const { return read_port<T>(port); }
        T operator()() const { return read(); }
        in_port& operator>>(T& value) const { value = read(); return *this; }

        const port_num port;
    };

    template <typename T = std::byte>
    struct out_port
    {
        void write(const T& value) const { write_port(port, value); }
        void operator()(const T& value) const { write(value); }
        out_port& operator<<(const T& value) const { write(value); return *this; }

        const port_num port;
    };

    template <typename T = std::byte>
    struct io_port
    {
        T read() const { return read_port<T>(port); }
        void write(const T& value) const { write_port(port, value); }
        T operator()() const { return read(); }
        void operator()(T value) const { return write(value); }
        io_port& operator>>(T& value) const { value = read(); return *this; }
        io_port& operator<<(const T& value) const { write(value); return *this; }

        const port_num port;
    };
}
