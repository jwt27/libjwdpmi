/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/audio/sample.h>
#include <jw/io/ioport.h>

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
