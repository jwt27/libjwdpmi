/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2023 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <jw/video/vga.h>
#include <jw/dpmi/realmode.h>

namespace jw::video
{
    static constexpr io::in_port<bool> dac_state { 0x3c7 };
    static constexpr io::io_port<byte> dac_write_index { 0x3c8 };
    static constexpr io::out_port<byte> dac_read_index { 0x3c7 };
    static constexpr io::io_port<byte> dac_data { 0x3c9 };

    void vga_bios::set_mode(vbe_mode m, const crtc_info *)
    {
        if (m.dont_clear_video_memory) m.index |= 0x80;
        dpmi::realmode_registers reg { };
        reg.ah = 0x00;
        reg.al = m.index;
        reg.call_int(0x10);
    }

    void vga::palette(std::span<const px32n> pal, std::size_t first, bool)
    {
        dac_write_index.write(first);
        if (dac_bits == 8)
        {
            for (const auto& i : pal)
            {
                dac_data.write(i.r);
                dac_data.write(i.g);
                dac_data.write(i.b);
            }
        }
        else
        {
            mmx_function<default_simd()>([pal]<simd flags>()
            {
                simd_pipeline pipe { simd_in, px_convert<pxvga>, simd_out };
                for (const auto& i : pal)
                {
                    const auto p = simd_run<flags>(pipe, i);
                    dac_data.write(p.r);
                    dac_data.write(p.g);
                    dac_data.write(p.b);
                }
            });
        }
    }

    std::array<px32n, 256> vga::palette()
    {
        std::array<px32n, 256> result;
        dac_read_index.write(0);
        if (dac_bits == 8)
        {
            for (auto i = 0; i < 256; ++i)
            {
                result[i].r = dac_data.read();
                result[i].g = dac_data.read();
                result[i].b = dac_data.read();
            }
        }
        else
        {
            mmx_function<default_simd()>([p = result.data()]<simd flags>()
            {
                simd_pipeline pipe { simd_in, px_convert<px32n>, simd_out };
                for (auto i = 0; i < 256; ++i)
                {
                    auto r = dac_data.read();
                    auto g = dac_data.read();
                    auto b = dac_data.read();
                    p[i] = simd_run<flags>(pipe, pxvga { r, g, b });
                }
            });
        }
        return result;
    }
}
