#pragma once
#include <cstdint>

namespace jw
{
    template<std::size_t size>
    union [[gnu::packed]] split_uint
    {
        struct [[gnu::packed]]
        {
            split_uint<size / 2> lo;
            split_uint<size / 2> hi;
        };
        std::uint64_t value : size;
        constexpr split_uint() noexcept : value(0) { }
        constexpr split_uint(auto v) noexcept : value(v) { };
        constexpr operator auto() const noexcept { return value; }
    };

    template<>
    union [[gnu::packed]] split_uint<8>
    {
        std::uint8_t value;
        constexpr split_uint() noexcept : value(0) { }
        constexpr split_uint(auto v) noexcept : value(v) { }
        constexpr operator auto() const noexcept { return value; }
    };

    using split_uint16_t = split_uint<16>;
    using split_uint32_t = split_uint<32>;
    using split_uint64_t = split_uint<64>;

    union [[gnu::packed]] split_int14_t
    {                      
        struct[[gnu::packed]]
        {
            unsigned lo : 7;
            unsigned hi : 7;
        };
        signed value : 14;
        constexpr split_int14_t() noexcept : value(0) { }
        constexpr split_int14_t(auto v) noexcept : value(v) { }
        constexpr operator auto() const noexcept { return value; }
    };
}