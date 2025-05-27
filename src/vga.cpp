/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2017 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#include <jw/video/vga.h>
#include <jw/dpmi/realmode.h>
#include <jw/io/ioport.h>
#include <jw/io/pci.h>

namespace jw::video
{
    static constexpr io::in_port<bool> dac_state { 0x3c7 };
    static constexpr io::io_port<std::uint8_t> dac_write_index { 0x3c8 };
    static constexpr io::out_port<std::uint8_t> dac_read_index { 0x3c7 };
    static constexpr io::io_port<std::uint8_t> dac_data { 0x3c9 };

    void vga_bios::set_mode(vbe_mode m, const crtc_info *)
    {
        if (m.dont_clear_video_memory) m.index |= 0x80;
        dpmi::realmode_registers reg;
        reg.ss = reg.sp = 0;
        reg.ah = 0x00;
        reg.al = m.index;
        reg.call_int(0x10);
    }

    std::optional<std::uint8_t> vga::find_irq()
    {
        struct vga : io::pci_device
        {
            vga() : pci_device { class_tag { }, 0x03, { 0x00, 0x01 }, 0 } { }

            std::uint8_t find_irq() const
            {
                auto info = bus_info().read();
                if (info.irq < 16)
                    return info.irq;

                info.irq = 9;
                bus_info().write(info);
                return 9;
            }
        };

        try
        {
            return vga { }.find_irq();
        }
        catch (const io::pci_device::error&)
        {
            return std::nullopt;
        }
    }

    void vga::palette(std::span<const px32n> pal, std::size_t first, bool)
    {
        std::array<std::uint8_t, 3 * 256> buf;
        if (dac_bits == 8)
        {
            for (unsigned i = 0; i != pal.size(); ++i)
            {
                buf[i * 3 + 0] = pal[i].r;
                buf[i * 3 + 1] = pal[i].g;
                buf[i * 3 + 2] = pal[i].b;
            }
        }
        else
        {
            mmx_function<default_simd()>([dst = buf.data(), pal]<simd flags>()
            {
                auto pipe = simd_in | px_convert<pxvga> | simd_out;
                for (unsigned i = 0; i != pal.size(); ++i)
                {
                    const auto p = std::get<0>(simd_run<flags>(pipe, pal[i]));
                    dst[i * 3 + 0] = p.r;
                    dst[i * 3 + 1] = p.g;
                    dst[i * 3 + 2] = p.b;
                }
            });
        }
        dac_write_index.write(first);
        dac_data.write(buf.data(), pal.size() * 3);
    }

    std::array<px32n, 256> vga::palette()
    {
        std::array<std::uint8_t, 3 * 256> buf;
        std::array<px32n, 256> result;
        dac_read_index.write(0);
        dac_data.read(buf.data(), buf.size());
        if (dac_bits == 8)
        {
            for (auto i = 0; i < 256; ++i)
            {
                result[i].r = buf[i * 3 + 0];
                result[i].g = buf[i * 3 + 1];
                result[i].b = buf[i * 3 + 2];
            }
        }
        else
        {
            mmx_function<default_simd()>([dst = result.data(), src = buf.data()]<simd flags>()
            {
                auto pipe = simd_in | px_convert<px32n> | simd_out;
                for (auto i = 0; i < 256; ++i)
                {
                    const auto r = src[i * 3 + 0];
                    const auto g = src[i * 3 + 1];
                    const auto b = src[i * 3 + 2];
                    const px32n color { r, g, b };
                    dst[i] = std::get<0>(simd_run<flags>(pipe, std::bit_cast<pxvga>(color)));
                }
            });
        }
        return result;
    }
}
