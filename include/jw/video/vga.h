/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2023 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <array>
#include <span>
#include <jw/video/vbe_types.h>
#include <jw/video/pixel.h>
#include <jw/io/ioport.h>

namespace jw::video
{
    struct vga_bios
    {
        virtual void set_mode(vbe_mode m, const crtc_info* = nullptr);
    };

    struct vga : public vga_bios
    {
        virtual void set_palette(std::span<const px32n>, std::size_t first = 0, bool wait_for_vsync = false);

        virtual std::array<px32n, 256> get_palette();

    protected:
        std::size_t dac_bits { 6 };
    };
}
