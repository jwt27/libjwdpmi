#pragma once
#include <cstdint>

using byte = std::uint8_t;
constexpr std::uint64_t operator""  _B(std::uint64_t n) { return n << 00; }
constexpr std::uint64_t operator"" _KB(std::uint64_t n) { return n << 10; }
constexpr std::uint64_t operator"" _MB(std::uint64_t n) { return n << 20; }
constexpr std::uint64_t operator"" _GB(std::uint64_t n) { return n << 30; }
constexpr std::uint64_t operator"" _TB(std::uint64_t n) { return n << 40; }

