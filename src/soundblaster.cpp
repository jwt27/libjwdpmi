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

namespace jw::audio::detail
{
    static bool dsp_read_ready(io::port_num base)
    {
        return io::read_port<std::uint8_t>(base + 0x0e) & 0x80;
    }

    static bool dsp_write_ready(io::port_num base)
    {
        return (io::read_port<std::uint8_t>(base + 0x0c) & 0x80) == 0;
    }

    static std::uint8_t dsp_force_read(io::port_num base)
    {
        return io::read_port<std::uint8_t>(base + 0x0a);
    }

    static void dsp_force_write(io::port_num base, std::uint8_t data)
    {
        io::write_port(base + 0x0c, data);
    }

    sb_dsp::sb_dsp(io::port_num p) : base { p }
    {
        reset();
    }

    void sb_dsp::reset()
    {
        using namespace std::chrono_literals;
        io::out_port<std::uint8_t> reset { base + 0x06 };

        reset.write(1);
        this_thread::yield_for(5us);
        reset.write(0);

        bool timeout = this_thread::yield_while_for([p = base] { return not dsp_read_ready(p); }, 125us);
        if (timeout or dsp_force_read(base) != 0xaa)
            throw io::device_not_found { "Sound Blaster not detected" };
    }

    std::uint8_t sb_dsp::read()
    {
        this_thread::yield_while([p = base] { return not dsp_read_ready(p); });
        return dsp_force_read(base);
    }

    void sb_dsp::write(std::uint8_t data)
    {
        this_thread::yield_while([p = base] { return not dsp_write_ready(p); });
        dsp_force_write(base, data);
    }

    sample_u8 sb_dsp::direct_in()
    {
        write(0x20);
        return read();
    }

    void sb_dsp::direct_out(sample_u8 sample)
    {
        write(0x10);
        write(sample);
    }
}

namespace jw::audio
{
    void soundblaster_config::read_blaster()
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
