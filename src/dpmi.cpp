/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

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
