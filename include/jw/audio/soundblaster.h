/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/audio/sample.h>
#include <jw/io/ioport.h>
#include <jw/split_int.h>

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

    // Basic Sound Blaster driver for "direct mode".  In this mode, you simply
    // write samples directly to the DAC.  This is typically done from the
    // timer interrupt, to achieve a stable sample rate.  Only 8-bit mono
    // samples are supported in this mode.
    struct sb_direct
    {
        explicit sb_direct(io::port_num base);
        explicit sb_direct(sb_config cfg)
            : sb_direct { cfg.base } { }

        sample_u8 in();
        void out(sample_u8 smp);

    private:
        const io::port_num dsp;
    };
}
