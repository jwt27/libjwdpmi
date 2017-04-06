/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <jw/video/vga.h>
#include <jw/dpmi/realmode.h>

namespace jw
{
    namespace video
    {
        constexpr io::in_port<bool>  vga::dac_state;
        constexpr io::io_port<byte>  vga::dac_write_index;
        constexpr io::out_port<byte> vga::dac_read_index;
        constexpr io::io_port<byte>  vga::dac_data;

        void bios::set_mode(vbe_mode m, const crtc_info *)
        {
            if (m.dont_clear_video_memory) m.mode |= 0x80;
            dpmi::realmode_registers reg { };
            reg.ah = 0x00;
            reg.al = m.mode;
            reg.call_int(0x10);
        }

        void vga::set_palette_data(std::vector<px32>::const_iterator begin, std::vector<px32>::const_iterator end, std::uint8_t first, bool)
        {
            dac_write_index.write(first);
            if (dac_bits == 8)
            {
                for (auto i = begin; i < end; ++i)
                {
                    dac_data.write(i->r);
                    dac_data.write(i->g);
                    dac_data.write(i->b);
                }
            }
            else
            {
                for (auto i = begin; i < end; ++i)
                {
                    auto p = static_cast<const pxvga>(*i);
                    dac_data.write(p.r);
                    dac_data.write(p.g);
                    dac_data.write(p.b);
                }
            }
        }

        std::vector<px32> vga::get_palette_data()
        {
            std::vector<px32> result { };
            dac_read_index.write(0);
            if (dac_bits == 8)
            {
                for (auto i = 0; i < 256; ++i)
                {
                    px32 value { 0,0,0,255 };
                    value.r = dac_data.read();
                    value.g = dac_data.read();
                    value.b = dac_data.read();
                    result.push_back(value);
                }
            }
            else
            {
                for (auto i = 0; i < 256; ++i)
                {
                    pxvga value { 0,0,0,255 };
                    value.r = dac_data.read();
                    value.g = dac_data.read();
                    value.b = dac_data.read();
                    result.push_back(value);
                }
            }
            result[0].a = 0;
            return result;
        }
    }
}
