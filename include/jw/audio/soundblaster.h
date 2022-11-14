/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/audio/sample.h>
#include <jw/audio/device.h>
#include <jw/io/ioport.h>
#include <jw/io/dma.h>
#include <jw/dpmi/irq_handler.h>
#include <jw/split_int.h>
#include <jw/function.h>
#include <optional>

namespace jw::audio
{
    struct sb_config
    {
        io::port_num base = 0x220;
        std::uint8_t irq = 5;
        std::uint8_t low_dma = 1;
        std::uint8_t high_dma = 5;

        // Read the above values from the BLASTER environment variable.
        // Throws std::runtime_error if BLASTER is unset or malformed.
        void read_blaster();
    };

    enum class sb_model
    {
        none,   // Not detected
        sb1,    // Sound Blaster 1.x
        sb2,    // Sound Blaster 2.0
        sbpro,  // Sound Blaster Pro or Pro2
        sb16    // Sound Blaster 16
    };

    struct sb_capabilities
    {
        split_uint16_t dsp_version;

        sb_model model() const noexcept
        {
            switch (dsp_version.hi)
            {
            case 4: return sb_model::sb16;
            case 3: return sb_model::sbpro;
            case 2: if (dsp_version.lo > 0) return sb_model::sb2;
            case 1: return sb_model::sb1;
            default: return sb_model::none;
            }
        }

        bool stereo() const noexcept
        {
            return dsp_version.hi >= 3;
        }
    };

    // Detect capabilities of Sound Blaster at specified address.
    sb_capabilities detect_sb(io::port_num);

    // Calculate effective sample rate for SB Pro 2 and earlier models, which
    // do not support exact sample rates.
    constexpr long double sb_sample_rate(unsigned rate, bool stereo)
    {
        const unsigned ch = stereo ? 2 : 1;
        const unsigned tc = ((0x10080 - (256'000'000 / (ch * rate))) >> 8) & 0xff;
        return -256e6L / ((tc << 8) - 0x10000) / ch;
    }
}

namespace jw::audio::detail
{
    enum class sb_state
    {
        idle,
        dma8_single,
        dma8,
        dma8_highspeed,
        dma16,
        stopping
    };

    template<any_sample_type_of<sample_u8, sample_i16> T>
    struct sb_driver final : device<T>::driver
    {
        sb_driver(sb_config cfg);
        virtual ~sb_driver();

        virtual void start(const start_parameters&) override;
        virtual void stop() override;
        virtual device<T>::buffer_type buffer() override;

        const split_uint16_t version;
        const io::port_num dsp;
        dpmi::irq_handler irq;
        io::dma8_channel dma8;
        std::optional<io::dma16_channel> dma16;
        std::optional<io::dma_buffer<T>> buf;
        sb_state state { sb_state::idle };
        bool stereo;
        bool recording;
        bool buffer_page_high;
        bool buffer_pending;
    };
}

namespace jw::audio
{
    template<sample_type T>
    inline auto soundblaster(sb_config cfg)
    {
        return device<T> { new (locked) detail::sb_driver<T> { cfg } };
    }

    // Driver for all Sound Blaster models.
    inline auto soundblaster_8 (sb_config cfg) { return soundblaster<sample_u8 >(cfg); }

    // Driver for Sound Blaster 16 only.
    inline auto soundblaster_16(sb_config cfg) { return soundblaster<sample_i16>(cfg); }

    // Basic Sound Blaster driver for "direct mode".  In this mode, you simply
    // write samples directly to the DAC.  This is typically done from the
    // timer interrupt, to achieve a stable sample rate.  Only 8-bit mono
    // samples are supported in this mode.
    struct soundblaster_pio final : pio_device<sample_u8, 1>
    {
        explicit soundblaster_pio(io::port_num base);
        explicit soundblaster_pio(sb_config cfg)
            : soundblaster_pio { cfg.base } { }

        virtual ~soundblaster_pio() = default;

        virtual std::array<sample_u8, 1> in() override;
        virtual void out(std::array<sample_u8, 1> smp) override;

    private:
        const io::port_num dsp;
    };
}
