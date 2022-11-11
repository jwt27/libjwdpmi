/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#include <jw/audio/soundblaster.h>
#include <jw/io/io_error.h>
#include <jw/chrono.h>
#include <jw/thread.h>
#include <string_view>
#include <charconv>
#include <system_error>
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace jw::audio
{
    static bool dsp_read_ready(io::port_num dsp)
    {
        return io::read_port<std::uint8_t>(dsp | 0x0e) & 0x80;
    }

    static bool dsp_write_ready(io::port_num dsp)
    {
        return (io::read_port<std::uint8_t>(dsp | 0x0c) & 0x80) == 0;
    }

    static std::uint8_t dsp_force_read(io::port_num dsp)
    {
        return io::read_port<std::uint8_t>(dsp | 0x0a);
    }

    static void dsp_force_write(io::port_num dsp, std::uint8_t data)
    {
        io::write_port(dsp | 0x0c, data);
    }

    static std::uint8_t dsp_read(io::port_num dsp)
    {
        this_thread::yield_while([dsp] { return not dsp_read_ready(dsp); });
        return dsp_force_read(dsp);
    }

    static void dsp_write(io::port_num dsp, std::uint8_t data)
    {
        this_thread::yield_while([dsp] { return not dsp_write_ready(dsp); });
        dsp_force_write(dsp, data);
    }

    static void dsp_reset(io::port_num dsp)
    {
        using namespace std::chrono_literals;
        io::out_port<std::uint8_t> reset { dsp | 0x06 };

        reset.write(1);
        this_thread::yield_for(5us);
        reset.write(0);

        bool timeout = this_thread::yield_while_for([dsp] { return not dsp_read_ready(dsp); }, 125us);
        if (timeout or dsp_force_read(dsp) != 0xaa)
            throw io::device_not_found { "Sound Blaster not detected" };
    }

    static split_uint16_t dsp_version(io::port_num dsp)
    {
        dsp_write(dsp, 0xe1);
        auto hi = dsp_read(dsp);
        auto lo = dsp_read(dsp);
        return { lo, hi };
    }

    static void dsp_speaker_enable(io::port_num dsp, bool on)
    {
        dsp_write(dsp, 0xd1 | (on << 1));
    }

    sb_direct::sb_direct(io::port_num base)
        : dsp { base }
    {
        dsp_reset(dsp);
        dsp_speaker_enable(dsp, true);
    }

    void sb_direct::out(sample_u8 sample)
    {
        dsp_write(dsp, 0x10);
        dsp_write(dsp, sample);
    }

    sample_u8 sb_direct::in()
    {
        dsp_write(dsp, 0x20);
        return dsp_read(dsp);
    }

    void sb_config::read_blaster()
    {
        const char* const blaster = std::getenv("BLASTER");
        if (blaster == nullptr or blaster[0] == '\0') throw std::runtime_error { "BLASTER unset" };
        const char* const end = blaster + std::strlen(blaster);
        for (const char* p = blaster; p < end;)
        {
            auto parse = [&](auto& value, int base)
            {
                const char* const q = std::find_if(p, end, [](char c) { return c == ' ' or (c >= 'A' and c <= 'Z'); });
                auto r = std::from_chars(p, q, value, base);
                if (auto err = std::make_error_code(r.ec)) throw std::system_error { err, "BLASTER malformed" };
                p = r.ptr;
            };

            int value;
            switch (*p++)
            {
            case 'A':
                parse(value, 16);
                if (value < 0x200 or value > 0x2f0) throw std::runtime_error { "BLASTER: Invalid base address" };
                base = value;
                break;
            case 'I':
                parse(value, 10);
                if (value < 0 or value > 15) throw std::runtime_error { "BLASTER: Invalid IRQ" };
                irq = value;
                break;
            case 'D':
                parse(value, 10);
                if (value < 0 or value > 3) throw std::runtime_error { "BLASTER: Invalid low DMA" };
                low_dma = value;
                break;
            case 'H':
                parse(value, 10);
                if (value < 5 or value > 7) throw std::runtime_error { "BLASTER: Invalid high DMA" };
                high_dma = value;
                break;
            }
        }
    }
}
