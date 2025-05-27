/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2016 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <span>

namespace jw::io
{
    using port_num = std::uint_fast16_t;

    template<typename T> requires (std::is_trivial_v<T>)
    inline T read_port(port_num p)
    {
        T v;
        asm volatile ("in %0, %w1" : "=a" (v) : "Nd" (p));
        return v;
    }

    template<typename T> requires (std::is_trivial_v<T>)
    inline T* read_port(std::type_identity_t<T>* ptr, std::size_t n, port_num p)
    {
        static_assert (sizeof(T) == 1 or sizeof(T) == 2 or sizeof(T) == 4);
        const auto* const end = ptr + n;
        if constexpr (sizeof(T) == 1)
            asm volatile ("cld; rep insb" : "+D" (ptr), "=o" (*ptr), "+c" (n) : "d" (p));
        if constexpr (sizeof(T) == 2)
            asm volatile ("cld; rep insw" : "+D" (ptr), "=o" (*ptr), "+c" (n) : "d" (p));
        if constexpr (sizeof(T) == 4)
            asm volatile ("cld; rep insd" : "+D" (ptr), "=o" (*ptr), "+c" (n) : "d" (p));
        [[assume(ptr == end)]];
        [[assume(n == 0)]];
        return ptr;
    }

    template<typename T> requires (std::is_trivial_v<T>)
    inline void write_port(port_num p, const std::type_identity_t<T>& v) noexcept
    {
        asm volatile ("out %w0, %1" :: "Nd" (p), "a" (v));
    }

    template<typename T> requires (std::is_trivial_v<T>)
    inline const T* write_port(port_num p, const std::type_identity_t<T>* ptr, std::size_t n) noexcept
    {
        static_assert (sizeof(T) == 1 or sizeof(T) == 2 or sizeof(T) == 4);
        const auto* const end = ptr + n;
        if constexpr (sizeof(T) == 1)
            asm volatile ("cld; rep outsb" : "+S" (ptr), "+c" (n) : "d" (p), "o" (*ptr));
        if constexpr (sizeof(T) == 2)
            asm volatile ("cld; rep outsw" : "+S" (ptr), "+c" (n) : "d" (p), "o" (*ptr));
        if constexpr (sizeof(T) == 4)
            asm volatile ("cld; rep outsd" : "+S" (ptr), "+c" (n) : "d" (p), "o" (*ptr));
        [[assume(ptr == end)]];
        [[assume(n == 0)]];
        return ptr;
    }

    template <typename T = std::byte>
    struct in_port
    {
        T read() const                                  { return read_port<T>(port); }
        T* read(T* p, std::size_t n) const              { return read_port<T>(p, n, port); }
        T* read(std::span<T> span) const                { return read_port<T>(span.data(), span.size(), port); }

        const port_num port;
    };

    template <typename T = std::byte>
    struct out_port
    {
        void write(const T& value) const                { return write_port<T>(port, value); }
        const T* write(const T* p, std::size_t n) const { return write_port<T>(port, p, n); }
        const T* write(std::span<const T> span) const   { return write_port<T>(port, span.data(), span.size()); }

        const port_num port;
    };

    template <typename T = std::byte>
    struct io_port
    {
        T read() const                                  { return read_port<T>(port); }
        T* read(T* p, std::size_t n) const              { return read_port<T>(p, n, port); }
        T* read(std::span<T> span) const                { return read_port<T>(span.data(), span.size(), port); }
        void write(const T& value) const                { return write_port<T>(port, value); }
        const T* write(const T* p, std::size_t n) const { return write_port<T>(port, p, n); }
        const T* write(std::span<const T> span) const   { return write_port<T>(port, span.data(), span.size()); }

        constexpr operator in_port<T>() const noexcept  { return { port }; }
        constexpr operator out_port<T>() const noexcept { return { port }; }

        const port_num port;
    };
}
