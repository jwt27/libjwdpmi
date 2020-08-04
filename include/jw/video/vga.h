/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <vector>
#include <jw/video/vbe_types.h>
#include <jw/video/pixel.h>
#include <jw/io/ioport.h>

namespace jw
{
    namespace video
    {
        struct vga_bios
        {
            virtual void set_mode(vbe_mode m, const crtc_info* = nullptr);
        };

        struct vga : public vga_bios
        {
            virtual void set_palette(const px32n* begin, const px32n* end, std::size_t first = 0, bool wait_for_vsync = false);
            virtual void set_palette(const std::vector<px32n>& data, std::size_t first = 0, bool wait_for_vsync = false)
            {
                set_palette(&*data.cbegin(), &*data.cend(), first, wait_for_vsync);
            }

            virtual std::vector<px32n> get_palette();

        protected:
            static inline constexpr io::in_port<bool> dac_state { 0x3c7 };
            static inline constexpr io::io_port<byte> dac_write_index { 0x3c8 };
            static inline constexpr io::out_port<byte> dac_read_index { 0x3c7 };
            static inline constexpr io::io_port<byte> dac_data { 0x3c9 };

            std::size_t dac_bits { 6 };
        };
    }
}
