/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */

#include <jw/audio/opl.h>
#include <jw/io/io_error.h>

namespace jw::audio
{
    basic_opl::basic_opl(io::port_num port)
        : status_register { port }
        , index { port + 0, port + 2 }
        , data { port + 1, port + 3 }
        , type { detect() }
    {
        common_registers c { };
        c.mask_timer0 = true;
        c.mask_timer1 = true;
        c.enable_opl3 = type != opl_type::opl2;
        c.enable_opl3_l = type == opl_type::opl3_l;
        write(c);
    }

    void basic_opl::reset()
    {
        for (unsigned i = 0; i < 18; ++i)
        {
            channel c { channels[i].value };
            c.key_on = false;
            c.freq_block = 0;
            c.freq_num = 0;
            write(c, i);
        }
        common_registers c { };
        c.mask_timer0 = true;
        c.mask_timer1 = true;
        c.enable_opl3 = common.value.enable_opl3;
        c.enable_opl3_l = common.value.enable_opl3_l;
        write(c);
        c.reset_irq = true;
        write(c);
    }

    opl_type basic_opl::detect()
    {
        using namespace std::chrono_literals;
        auto w = [this] (unsigned r, unsigned v) { do_write<opl_type::opl2>(r, static_cast<std::byte>(v)); };
        auto w_opl3 = [this] (unsigned r, unsigned v) { do_write<opl_type::opl3>(r, static_cast<std::byte>(v)); };

        // from https://www.fit.vutbr.cz/~arnost/opl/opl3.html#appendixB
        w(0x04, 0x60);          // mask both timers
        w(0x04, 0x80);          // reset irq
        if (status().timer0) throw io::device_not_found { "OPL not detected" };
        w(0x02, 0xff);          // set timer 0 count 0xff
        w(0x04, 0x21);          // start timer 0
        thread::yield_for(80us);
        auto s = status();
        if (not s.timer0) throw io::device_not_found { "OPL not detected" };
        w(0x04, 0x60);          // mask both timers
        w(0x04, 0x80);          // reset irq
        if (s.opl2) return opl_type::opl2;

        w_opl3(0x02, 0xa5);     // write a distinctive value to timer 0
        thread::yield_for(2235ns);
        if (data[0].read() == std::byte { 0xa5 }) return opl_type::opl3_l;
        else return opl_type::opl3;
    }

    void basic_opl::write(const common_registers& value)
    {
        write<0x01, 0x02, 0x03, 0x04, 0x08, 0xbd, 0x101, 0x104, 0x105>(value, common, 0);
        common.value.reset_irq = false;
        common.value.hihat = false;
        common.value.top_cymbal = false;
        common.value.tomtom = false;
        common.value.snare_drum = false;
        common.value.bass_drum = false;
    }

    void basic_opl::write(const oscillator& value, std::uint8_t slot)
    {
        if (slot >= 18) write<0x120, 0x140, 0x160, 0x180, 0x1e0>(value, oscillators[slot], slot - 18);
        else write<0x20, 0x40, 0x60, 0x80, 0xe0>(value, oscillators[slot], slot);
    }

    void basic_opl::write(const channel& value, std::uint8_t ch)
    {
        if (type != opl_type::opl2)
        {
            auto enable_4op = read().enable_4op.bitset();
            auto ch_4op = lookup_2to4(ch);
            if (ch_4op != 0xff and ch == lookup_4to2_sec(ch_4op) and enable_4op[ch_4op])
            {
                if (ch >= 9) write<0x1c0>(value, channels[ch], ch - 9);
                else write<0xc0>(value, channels[ch], ch);
                return;
            }
        }

        if (ch >= 9) write<0x1c0, 0x1a0, 0x1b0>(value, channels[ch], ch - 9);
        else write<0xc0, 0xa0, 0xb0>(value, channels[ch], ch);
    }

    template<unsigned... R, typename T>
    void basic_opl::write(const T& v, cached_reg<T>& cache, unsigned offset)
    {
        static_assert(sizeof...(R) <= sizeof(T));
        static constexpr unsigned regnum[] { R... };
        const reg<T> value { v };
        for (unsigned i = 0; i < sizeof...(R); ++i)
        {
            if (value.raw[i] == cache.raw[i] and cache.written[i]) continue;
            write(regnum[i] + offset, value.raw[i]);
            cache.raw[i] = value.raw[i];
        }
    }

    void basic_opl::write(std::uint16_t reg, std::byte value)
    {
        switch (type)
        {
        case opl_type::opl2: return do_write<opl_type::opl2>(reg, value);
        case opl_type::opl3: return do_write<opl_type::opl3>(reg, value);
        case opl_type::opl3_l: return do_write<opl_type::opl3_l>(reg, value);
        }
    }

    template<opl_type t>
    void basic_opl::do_write(std::uint16_t reg, std::byte value)
    {
        using namespace std::chrono_literals;
        constexpr bool opl2 = t == opl_type::opl2;
        constexpr bool opl3 = t == opl_type::opl3;
        constexpr bool opl3_l = t == opl_type::opl3_l;

        const bool hi = reg > 0xff;
        reg &= 0xff;
        if constexpr (opl2) if (hi) return;

        if constexpr (opl3_l) thread::yield_while([this] { return status().busy; });
        else thread::yield_while([this] { return clock::now() < last_access + (opl3 ? 2235ns : 23us); });

        if (current_index[hi] != reg) [[likely]]
        {
            index[hi].write(reg);
            if constexpr (opl2) last_access = clock::now();
            current_index[hi] = reg;
            if constexpr (opl2) thread::yield_while([this] { return clock::now() < last_access + 3300ns; });
        }

        data[hi].write(value);
        if constexpr (opl2 or opl3) last_access = clock::now();
    }
}
