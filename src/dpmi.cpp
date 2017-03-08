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

#include <jw/dpmi/dpmi.h>

namespace jw
{
    namespace dpmi
    {
        bool capabilities::init { false };
        bool capabilities::sup { true };
        std::uint16_t capabilities::raw_flags;
        std::array<byte, 128> capabilities::raw_vendor_info;

        split_uint16_t version::ax;
        split_uint16_t version::dx;
        std::uint16_t version::bx;
        std::uint8_t version::cl;
        bool version::init { false };
    }
}
