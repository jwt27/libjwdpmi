/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <jw/video/vga.h>
#include <jw/dpmi/realmode.h>

namespace jw
{
    namespace video
    {
        void bios::set_mode(vbe_mode m, const crtc_info *)
        {
            if (m.dont_clear_video_memory) m.mode |= 0x80;
            dpmi::realmode_registers reg { };
            reg.ah = 0x00;
            reg.al = m.mode;
            reg.call_int(0x10);
        }

        void vga::set_palette(const px32a* begin, const px32a* end, std::size_t first, bool)
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

        std::vector<px32a> vga::get_palette()
        {
            std::vector<px32a> result { };
            dac_read_index.write(0);
            if (dac_bits == 8)
            {
                for (auto i = 0; i < 256; ++i)
                {
                    auto r = dac_data.read();
                    auto g = dac_data.read();
                    auto b = dac_data.read();
                    result.push_back(px32a { r, g, b, px32a::ax });
                }
            }
            else
            {
                for (auto i = 0; i < 256; ++i)
                {
                    auto r = dac_data.read();
                    auto g = dac_data.read();
                    auto b = dac_data.read();
                    result.push_back(pxvga { r, g, b, pxvga::ax });
                }
            }
            result[0].a = 0;
            return result;
        }
    }
}
