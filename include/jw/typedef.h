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

using byte = std::uint8_t;
constexpr std::uint64_t operator""  _B(std::uint64_t n) { return n << 00; }
constexpr std::uint64_t operator"" _KB(std::uint64_t n) { return n << 10; }
constexpr std::uint64_t operator"" _MB(std::uint64_t n) { return n << 20; }
constexpr std::uint64_t operator"" _GB(std::uint64_t n) { return n << 30; }
constexpr std::uint64_t operator"" _TB(std::uint64_t n) { return n << 40; }

