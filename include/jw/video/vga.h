/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2017 - 2023 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <jw/video/vbe_types.h>
#include <jw/video/pixel.h>
#include <array>
#include <span>
#include <optional>

namespace jw::video
{
    struct vga_bios
    {
        virtual void set_mode(vbe_mode m, const crtc_info* = nullptr);
    };

    struct vga : public vga_bios
    {
        // Find the vertical retrace IRQ on PCI/AGP VGA cards.
        static std::optional<std::uint8_t> find_irq();

        virtual void palette(std::span<const px32n>, std::size_t first = 0, bool wait_for_vsync = false);

        virtual std::array<px32n, 256> palette();

    protected:
        std::size_t dac_bits { 6 };
    };
}
