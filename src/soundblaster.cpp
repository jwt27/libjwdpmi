/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2023 J.W. Jagersma, see COPYING.txt for details */
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
        return io::read_port<std::uint8_t>(dsp + 0x0e) & 0x80;
    }

    static bool dsp_write_ready(io::port_num dsp)
    {
        return (io::read_port<std::uint8_t>(dsp + 0x0c) & 0x80) == 0;
    }

    static std::uint8_t dsp_force_read(io::port_num dsp)
    {
        return io::read_port<std::uint8_t>(dsp + 0x0a);
    }

    static void dsp_force_write(io::port_num dsp, std::uint8_t data)
    {
        io::write_port(dsp + 0x0c, data);
    }

    template<bool yield>
    static std::uint8_t dsp_read(io::port_num dsp)
    {
        auto wait = [dsp] { return not dsp_read_ready(dsp); };
        if constexpr (yield) this_thread::yield_while(wait);
        else do { } while (wait());
        return dsp_force_read(dsp);
    }

    template<bool yield>
    static void dsp_write(io::port_num dsp, std::uint8_t data)
    {
        auto wait = [dsp] { return not dsp_write_ready(dsp); };
        if constexpr (yield) this_thread::yield_while(wait);
        else do { } while (wait());
        dsp_force_write(dsp, data);
    }

    static bool dsp_reset(io::port_num dsp)
    {
        using namespace std::chrono_literals;
        io::out_port<std::uint8_t> reset { dsp + 0x06 };

        reset.write(1);
        this_thread::yield_for(5us);
        reset.write(0);

        bool timeout = this_thread::yield_while_for([dsp] { return not dsp_read_ready(dsp); }, 125us);
        return not timeout and dsp_force_read(dsp) == 0xaa;
    }

    static void dsp_speaker_enable(io::port_num dsp, bool on)
    {
        dsp_write<true>(dsp, on ? 0xd1 : 0xd3);
    }

    static void dsp_init(io::port_num dsp)
    {
        if (dsp & 0xf)
            throw std::invalid_argument { "Invalid Sound Blaster port" };

        if (not dsp_reset(dsp))
            throw io::device_not_found { "Sound Blaster not detected" };
    }

    static split_uint16_t dsp_version(io::port_num dsp)
    {
        dsp_write<true>(dsp, 0xe1);
        auto hi = dsp_read<true>(dsp);
        auto lo = dsp_read<true>(dsp);
        return { lo, hi };
    }

    static void dsp_dma_time_constant(io::port_num dsp, std::uint8_t tc)
    {
        dsp_write<false>(dsp, 0x40);
        dsp_write<false>(dsp, tc);
    }

    static void dsp_dma_block_size(io::port_num dsp, split_uint16_t size)
    {
        dsp_write<false>(dsp, 0x48);
        dsp_write<false>(dsp, size.lo);
        dsp_write<false>(dsp, size.hi);
    }

    static void dsp_dma8_single(io::port_num dsp, bool input, split_uint16_t size)
    {
        dsp_write<false>(dsp, input ?  0x24 : 0x14);
        dsp_write<false>(dsp, size.lo);
        dsp_write<false>(dsp, size.hi);
    }

    static void dsp_dma8_auto(io::port_num dsp, bool input)
    {
        dsp_write<false>(dsp, input ? 0x2c : 0x1c);
    }

    static void dsp_dma8_auto_highspeed(io::port_num dsp, bool input)
    {
        dsp_write<false>(dsp, input ? 0x98 : 0x90);
    }

    static void dsp_sb16_dma8_auto(io::port_num dsp, bool input, bool stereo, split_uint16_t size)
    {
        dsp_write<false>(dsp, input ? 0xce : 0xc6);
        dsp_write<false>(dsp, stereo ? 0x20 : 0x00);
        dsp_write<false>(dsp, size.lo);
        dsp_write<false>(dsp, size.hi);
    }

    static void dsp_sb16_dma16_auto(io::port_num dsp, bool input, bool stereo, split_uint16_t size)
    {
        dsp_write<false>(dsp, input ? 0xbe : 0xb6);
        dsp_write<false>(dsp, stereo ? 0x30 : 0x10);
        dsp_write<false>(dsp, size.lo);
        dsp_write<false>(dsp, size.hi);
    }

    static void dsp_dma8_auto_stop(io::port_num dsp)
    {
        dsp_write<false>(dsp, 0xda);
    }

    static void dsp_dma16_auto_stop(io::port_num dsp)
    {
        dsp_write<false>(dsp, 0xd9);
    }

    static void dsp_input_stereo(io::port_num dsp, bool stereo)
    {
        dsp_write<false>(dsp, stereo ? 0xa8 : 0xa0);
    }

    static void dsp_sb16_sample_rate(io::port_num dsp, bool input, split_uint16_t rate)
    {
        dsp_write<false>(dsp, input ? 0x42 : 0x41);
        dsp_write<false>(dsp, rate.hi);
        dsp_write<false>(dsp, rate.lo);
    }

    static void mixer_index(io::port_num mx, std::uint8_t i)
    {
        io::write_port(mx + 0x04, i);
    }

    static std::uint8_t mixer_read(io::port_num mx)
    {
        return io::read_port<std::uint8_t>(mx + 0x05);
    }

    static void mixer_write(io::port_num mx, std::uint8_t data)
    {
        io::write_port(mx + 0x05, data);
    }

    static void mixer_set_stereo(io::port_num mx, bool stereo)
    {
        mixer_index(mx, 0x0c);
        std::bitset<8> a = mixer_read(mx);
        a[3] = true;    // Disable input filter
        a[5] = true;
        mixer_write(mx, a.to_ulong());
        mixer_index(mx, 0x0e);
        a = mixer_read(mx);
        a[5] = true;    // Disable output filter
        a[1] = stereo;
        mixer_write(mx, a.to_ulong());
    }

    sb_capabilities detect_sb(io::port_num base)
    {
        if (not dsp_reset(base))
            return { 0 };

        return { dsp_version(base) };
    }

    soundblaster_pio::soundblaster_pio(io::port_num base)
        : dsp { base }
    {
        dsp_init(dsp);
        dsp_speaker_enable(dsp, true);
        mixer_set_stereo(dsp, false);
    }

    void soundblaster_pio::out(std::array<sample_u8, 1> sample)
    {
        dsp_write<true>(dsp, 0x10);
        dsp_write<true>(dsp, sample[0]);
    }

    std::array<sample_u8, 1> soundblaster_pio::in()
    {
        dsp_write<true>(dsp, 0x20);
        return { dsp_read<true>(dsp) };
    }
}

namespace jw::audio::detail
{
    static auto sb_init(io::port_num dsp)
    {
        dsp_init(dsp);
        return dsp_version(dsp);
    }

    template<bool sb16, sb_state state, typename T>
    [[gnu::hot]] static void sb_irq(sb_driver<T>* drv)
    {
        const auto dsp = drv->dsp;

        if constexpr (sb16)
        {
            mixer_index(dsp, 0x82);
            std::bitset<8> irq_status = mixer_read(dsp);
            if (state == sb_state::dma8 and irq_status[0])
                io::read_port<bool>(dsp + 0x0e);
            else if (state == sb_state::dma16 and irq_status[1])
                io::read_port<bool>(dsp + 0x0f);
            else return;
        }

        if constexpr (state == sb_state::stopping)
        {
            drv->state = sb_state::idle;
            drv->irq.disable();
            dsp_speaker_enable(dsp, false);
        }
        else
        {
            if constexpr (state == sb_state::dma8_single)
            {
                dsp_dma8_single(dsp, drv->recording, drv->buf->size() / 2 - 1);
            }
            else if constexpr (not sb16) dsp_read_ready(dsp);

            drv->buffer_page_high ^= true;
            drv->buffer_pending = true;
            if (drv->callback) drv->callback(drv->buffer());
        }

        dpmi::irq_handler::acknowledge();
    }

    template<bool sb16, sb_state state, typename T>
    static auto make_sb_irq(sb_driver<T>* drv)
    {
        return [drv] { sb_irq<sb16, state>(drv); };
    }

    template<typename T>
    sb_driver<T>::sb_driver(sb_config cfg)
        : version { sb_init(cfg.base) }
        , dsp { cfg.base }
        , irq { cfg.irq, [] { }, dpmi::no_auto_eoi }
        , dma8 { cfg.low_dma }
    {
        if constexpr (std::same_as<T, sample_i16>)
        {
            if (version.hi < 4)
                throw io::device_not_found { "Sound Blaster 16 not detected" };
            if (cfg.high_dma > 3)
                dma16.emplace(cfg.high_dma);
        }
    }
    template<typename T>
    sb_driver<T>::~sb_driver()
    {
        stop();
        this_thread::yield_while([this] { return volatile_load(&state) == sb_state::stopping; });
    }

    template<typename T>
    void sb_driver<T>::start(const start_parameters& params)
    {
        if (state != sb_state::idle and state != sb_state::stopping)
            throw std::runtime_error { "Already started" };

        if (params.out.channels > 2 or params.in.channels > 2)
            throw std::invalid_argument { "Invalid number of channels" };

        if ((params.out.channels > 1 or params.in.channels > 1) and version.hi < 3)
            throw std::invalid_argument { "Stereo not supported" };

        if (params.out.channels > 0 and params.in.channels > 0)
            throw std::invalid_argument { "Full-duplex not supported" };

        if (params.out.channels == 0 and params.in.channels == 0)
            throw std::invalid_argument { "Neither input nor output specified" };

        recording = params.in.channels > 0;
        stereo = recording ? params.in.channels == 2 : params.out.channels == 2;
        const auto dir = recording ? io::dma_direction::from_device : io::dma_direction::to_device;
        const auto size = (recording ? params.in.buffer_size : params.out.buffer_size) * (stereo ? 2 : 1);

        if (size == 0)
            throw std::invalid_argument { "No buffer size specified" };

        if (not buf or buf->size() != size * 2)
            buf.emplace(size * 2);

        buffer_pending = false;
        buffer_page_high = true;

        if (not recording)
            std::fill_n(buf->pointer(), buf->size(), sample_traits<T>::zero());

        dsp_speaker_enable(dsp, not recording);

        dpmi::interrupt_mask no_irq { };
        irq.enable();

        if (sizeof(T) == 2 and dma16)
        {
            dma16->disable();
            dma16->transfer(*buf, io::dma_mode::auto_single, dir);
            dma16->enable();
        }
        else
        {
            dma8.disable();
            dma8.transfer(*buf, io::dma_mode::auto_single, dir);
            dma8.enable();
        }

        if constexpr (sizeof(T) == 2)
        {
            irq = make_sb_irq<true, sb_state::dma16>(this);
            irq.enable();
            state = sb_state::dma16;
            dsp_sb16_sample_rate(dsp, recording, params.sample_rate);
            dsp_sb16_dma16_auto(dsp, recording, stereo, size - 1);
        }
        else if (version.hi == 4)
        {
            irq = make_sb_irq<true, sb_state::dma8>(this);
            state = sb_state::dma8;
            dsp_sb16_sample_rate(dsp, recording, params.sample_rate);
            dsp_sb16_dma8_auto(dsp, recording, stereo, size - 1);
        }
        else
        {
            const auto rate = ((stereo ? 2 : 1) * params.sample_rate);
            const auto tc = (0x10080 - (256'000'000 / rate)) >> 8;
            dsp_dma_time_constant(dsp, tc);

            if (version.hi == 1)
            {
                irq = make_sb_irq<false, sb_state::dma8_single>(this);
                state = sb_state::dma8_single;
                dsp_dma8_single(dsp, recording, size - 1);
            }
            else
            {
                irq = make_sb_irq<false, sb_state::dma8>(this);
                mixer_set_stereo(dsp, stereo);
                if (version.hi == 3 and recording) dsp_input_stereo(dsp, stereo);
                dsp_dma_block_size(dsp, size - 1);

                bool dsp201 = (version.hi == 2 and version.lo > 0) or version.hi > 2;
                if (dsp201 and (stereo or params.sample_rate >= 23000))
                {
                    state = sb_state::dma8_highspeed;
                    dsp_dma8_auto_highspeed(dsp, recording);
                }
                else
                {
                    state = sb_state::dma8;
                    dsp_dma8_auto(dsp, recording);
                }
            }
        }
    }

    template<typename T>
    void sb_driver<T>::stop()
    {
        dpmi::interrupt_mask no_irq { };
        switch (state)
        {
        case sb_state::idle:
        case sb_state::stopping:
            return;

        case sb_state::dma8_single:
            irq = make_sb_irq<false, sb_state::stopping>(this);
            state = sb_state::stopping;
            return;

        case sb_state::dma8:
            dsp_dma8_auto_stop(dsp);
            break;

        case sb_state::dma16:
            dsp_dma16_auto_stop(dsp);
            break;

        case sb_state::dma8_highspeed:
            dsp_reset(dsp);
        }
        irq.disable();
        state = sb_state::idle;
        dsp_speaker_enable(dsp, false);
    }

    template<typename T>
    device<T>::buffer_type sb_driver<T>::buffer()
    {
        if (not buffer_pending) return { };
        buffer_pending = false;

        const unsigned ch = stereo ? 2 : 1;
        const unsigned n = buf->size() / 2;
        T* const p = buf->pointer() + (buffer_page_high ? n : 0u);

        if (recording)
            return { { p, n, ch }, { } };
        else
            return { { }, { p, n, ch } };
    }

    template struct sb_driver<sample_u8>;
    template struct sb_driver<sample_i16>;
}

namespace jw::audio
{
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
