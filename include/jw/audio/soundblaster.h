/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/audio/sample.h>
#include <jw/io/ioport.h>

namespace jw::audio::detail
{
    struct sb_dsp
    {
        explicit sb_dsp(io::port_num p);

        sb_dsp(sb_dsp&&) = default;
        sb_dsp& operator=(sb_dsp&&) = default;
        sb_dsp(const sb_dsp&) = delete;
        sb_dsp& operator=(const sb_dsp&) = delete;

        void reset();

        std::uint8_t read();
        void write(std::uint8_t);

        sample_u8 direct_in();
        void direct_out(sample_u8);

    private:
        const io::port_num base;
    };
}

namespace jw::audio
{
    // Basic Sound Blaster driver for "direct mode".  In this mode, you simply
    // write samples directly to the DAC.  This is typically done from the
    // timer interrupt, to achieve a stable sample rate.  Only 8-bit mono
    // samples are supported in this mode.
    struct soundblaster_direct
    {
        explicit soundblaster_direct(io::port_num base) : dsp { base } { }

        sample_u8 in() { return dsp.direct_in(); }
        void out(sample_u8 smp) { dsp.direct_out(smp); }

    private:
        detail::sb_dsp dsp;
    };
}
