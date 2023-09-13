/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2020 - 2023 J.W. Jagersma, see COPYING.txt for details    */

#pragma once

#include <bit>
#include <bitset>
#include <array>
#include <jw/mutex.h>
#include <jw/io/ioport.h>
#include <jw/chrono.h>
#include <jw/math.h>
#include "jwdpmi_config.h"

namespace jw::audio
{
    enum class opl_type
    {
        opl2, opl3, opl3_l
    };

    struct [[gnu::packed]] opl_status
    {
        bool busy : 1;
        bool opl2 : 1;
        bool busy2 : 1;
        unsigned : 2;
        bool timer1 : 1;
        bool timer0 : 1;
        bool irq : 1;
    };

    // Minimal OPL driver.  Provides direct register access, no caching.
    struct opl_driver
    {
        using clock = config::midi_clock;

        opl_driver(io::port_num);

        void write(unsigned index, std::byte data) { return do_write(this, index, data); }
        opl_status status() const noexcept { return io::read_port<opl_status>(base); }
        opl_type type() const noexcept { return opltype; }

    private:
        template<opl_type>
        static void write_impl(opl_driver*, unsigned, std::byte);
        opl_type detect();

        const io::port_num base;
        const opl_type opltype;
        void (* const do_write)(opl_driver*, unsigned, std::byte);
        unsigned index { 0 };
        clock::time_point last_access { clock::time_point::min() };
    };

    // Find absolute operator slot number for given operator in given channel.
    constexpr std::uint8_t opl_slot(std::uint8_t ch, std::uint8_t op) noexcept
    {
        assume(ch < 18 and op < 4);
        return ch + 3 * (ch / 3) + 3 * op;
    }

    // Find primary 2op channel number for given 4op channel number.
    constexpr std::uint8_t opl_4to2_pri(std::uint8_t ch_4op) noexcept
    {
        assume(ch_4op < 6);
        return (0xba9210u >> (ch_4op << 2)) & 0xf;
    }

    // Find secondary 2op channel number for given 4op channel number.
    constexpr std::uint8_t opl_4to2_sec(std::uint8_t ch_4op) noexcept
    {
        return opl_4to2_pri(ch_4op) + 3;
    }

    // Find 4op channel number that the given 2op channel is part of, or 0xff if none.
    constexpr std::uint8_t opl_2to4(std::uint8_t ch_2op) noexcept
    {
        constexpr std::uint8_t table[] { 0, 1, 2, 0, 1, 2, 0xff, 0xff, 0xff,
                                         3, 4, 5, 3, 4, 5, 0xff, 0xff, 0xff };
        return table[ch_2op];
    }

    struct basic_opl;

    struct [[gnu::packed]] opl_setup
    {
        unsigned test0 : 5;
        bool enable_waveform_select : 1;        // OPL2 only
        unsigned test1 : 2;
        unsigned : 6;
        bool note_sel : 1;
        bool enable_composite_sine_mode : 1;    // OPL2 only
        unsigned test_opl3 : 6;
        unsigned : 2;
        bool enable_opl3 : 1;
        unsigned : 1;
        bool enable_opl3_l : 1;
        unsigned : 5;
    };

    struct [[gnu::packed]] opl_timer
    {
        unsigned timer0 : 8;
        unsigned timer1 : 8;
        bool start_timer0 : 1;
        bool start_timer1 : 1;
        unsigned : 3;
        bool mask_timer1 : 1;
        bool mask_timer0 : 1;
        bool reset_irq : 1;
    };

    struct [[gnu::packed]] opl_4op
    {
        bool ch0 : 1;
        bool ch1 : 1;
        bool ch2 : 1;
        bool ch9 : 1;
        bool chA : 1;
        bool chB : 1;
        unsigned : 2;

        void bitset(std::bitset<6> value) noexcept { *reinterpret_cast<std::uint8_t*>(this) = value.to_ulong(); }
        constexpr std::bitset<6> bitset() const noexcept { return { *reinterpret_cast<const std::uint8_t*>(this) }; }
    };

    struct [[gnu::packed]] opl_percussion
    {
        bool hihat : 1;
        bool top_cymbal : 1;
        bool tomtom : 1;
        bool snare_drum : 1;
        bool bass_drum : 1;
        bool enable_percussion : 1;
        unsigned vibrato_depth : 1;
        unsigned tremolo_depth : 1;
    };

    struct [[gnu::packed]] opl_operator
    {
        unsigned multiplier : 4;
        bool key_scale_rate : 1;
        bool enable_sustain : 1;
        bool vibrato : 1;
        bool tremolo : 1;
        unsigned attenuation : 6;
        unsigned key_scale_level : 2;
        unsigned decay : 4;
        unsigned attack : 4;
        unsigned release : 4;
        unsigned sustain : 4;
        unsigned waveform : 3;
        unsigned : 5;
    };

    struct [[gnu::packed]] opl_channel
    {
        unsigned connection : 1;
        unsigned feedback : 3;
        bool output_ch0 : 1;    // left
        bool output_ch1 : 1;    // right
        bool output_ch2 : 1;
        bool output_ch3 : 1;
        unsigned freq_num : 10;
        unsigned freq_block : 3;
        bool key_on : 1;
        unsigned : 2;

        template<unsigned sample_rate>
        constexpr void freq(float) noexcept;
        void freq(const basic_opl& opl, float) noexcept;

        template<unsigned sample_rate, long double A4 = 440.L>
        void note(int) noexcept;
        template<long double A4 = 440.L>
        void note(const basic_opl&, int) noexcept;

        template<unsigned sample_rate, long double A4 = 440.L>
        void note(long double) noexcept;
        template<long double A4 = 440.L>
        void note(const basic_opl&, long double) noexcept;

        void output(std::bitset<4> value) noexcept;
        constexpr std::bitset<4> output() const noexcept;
    };

    struct basic_opl
    {
        using clock = opl_driver::clock;

        basic_opl(io::port_num port);
        virtual ~basic_opl() { reset(); }

        void write(const opl_setup&);
        void write(const opl_timer&);
        void write(const opl_4op&);
        void write(const opl_percussion&);
        void write(const opl_channel&, std::uint8_t ch);
        void write(const opl_operator&, std::uint8_t slot);
        void write(const opl_operator& o, std::uint8_t ch, std::uint8_t osc) { write(o, opl_slot(ch, osc)); }

        const opl_setup&      read_setup() const noexcept { return reg_setup.value; }
        const opl_timer&      read_timer() const noexcept { return reg_timer.value; }
        const opl_4op&        read_4op() const noexcept { return reg_4op.value; }
        const opl_percussion& read_percussion() const noexcept { return reg_percussion.value; }
        const opl_channel&    read_channel(std::uint8_t ch) const noexcept { return channels[ch].value; }
        const opl_operator&   read_operator(std::uint8_t osc) const noexcept { return operators[osc].value; }
        const opl_operator&   read_operator(std::uint8_t ch, std::uint8_t osc) const noexcept { return read_operator(opl_slot(ch, osc)); }

        bool is_4op(std::uint8_t ch_4op) const noexcept { return read_4op().bitset()[ch_4op]; };
        void set_4op(std::uint8_t ch_4op, bool enable);

        opl_type type() const noexcept { return drv.type(); }
        opl_status status() const noexcept { return drv.status(); }
        void reset();

    private:
        basic_opl(const basic_opl&) = delete;
        basic_opl(basic_opl&&) = delete;
        basic_opl& operator=(const basic_opl&) = delete;
        basic_opl& operator=(basic_opl&&) = delete;

        template <typename T>
        struct reg
        {
            union
            {
                T value;
                std::array<std::byte, sizeof(T)> raw;
            };

            constexpr reg() noexcept = default;
            constexpr reg(const T& v) noexcept : value { v } { }
        };

        template<bool force, unsigned... N, typename T>
        void write(const T&, reg<T>&, unsigned);
        template<bool force, unsigned I, unsigned N, unsigned... Next>
        void do_write(const std::byte*, std::byte*, unsigned);
        void init();

        opl_driver drv;
        reg<opl_setup> reg_setup;
        reg<opl_timer> reg_timer;
        reg<opl_4op> reg_4op;
        reg<opl_percussion> reg_percussion;
        std::array<reg<opl_operator>, 36> operators;
        std::array<reg<opl_channel>, 18> channels;
    };

    struct opl_config
    {
        // Try to allocate 2op channels so that they don't overlap with 4op
        // channels.  Has no effect for OPL2.
        //         no: No special treatment is given to 4op slots.
        //        yes: 2op channels may only be allocated in a 4op slot if all
        //             2op-only slots are taken.
        //      force: 2op channels are never allocated in a 4op slot.
        //  automatic: Default to 'no', switch to 'yes' when there are any
        //             active 4op channels.
        // auto_force: Default to 'no', latch to 'force' once a 4op channel is
        //             played.
        enum : std::uint8_t { no, yes, force, automatic, auto_force } prioritize_4op { automatic };

        // Ignore channel priority field.
        bool ignore_priority { false };

        // Determines how envelope rate scaling is calculated.
        //  true: use freq_num bit 8.
        // false: use freq_num bit 9.
        bool note_select { true };

        // For tremolo: low = 1dB, high = 4.8dB.
        // For vibrato: low = 7 cents, high = 14 cents.
        enum : std::uint8_t { low, high } tremolo_depth : 1 { low }, vibrato_depth : 1 { low };
    };

    struct opl final : private basic_opl
    {
        using base = basic_opl;
        using clock = base::clock;

        template<unsigned N>
        struct channel_base : opl_channel
        {
            static_assert(N == 2 or N == 4);

            std::uint8_t connection : N / 2;
            std::array<opl_operator, N> op;
            int priority;
        };

        template<unsigned N>
        struct channel final : channel_base<N>
        {
            using base = channel_base<N>;
            friend struct opl;

            constexpr channel() noexcept = default;
            ~channel() { if (allocated()) owner->remove(this); }

            channel(const channel&) noexcept;
            channel& operator=(const channel&) noexcept;
            channel(channel&& c) noexcept;
            channel& operator=(channel&& c) noexcept;

            void freq(const opl& o, float f) noexcept           { base::freq(o, f); }
            void note(const opl& o, int n) noexcept             { base::note(o, n); }
            void note(const opl& o, long double n) noexcept     { base::note(o, n); }
            bool key_on(opl& o)                                 { return o.insert(this); }
            void key_off()                                      { if (allocated()) owner->stop(this); }
            void update()                                       { if (allocated()) owner->update(this); }
            bool silent() const noexcept                        { return not allocated() or off_time < clock::now(); }
            bool silent_at(clock::time_point t) const noexcept  { return not allocated() or off_time < t; }
            bool allocated() const noexcept                     { return owner != nullptr; }

            static channel from_bytes(std::span<const std::byte, sizeof(base)>) noexcept;
            std::array<std::byte, sizeof(base)> to_bytes() const noexcept;

        private:
            channel(const base& c) noexcept : base { c } { };

            bool key_on() const noexcept { return base::key_on; }
            void key_on(bool v) noexcept { base::key_on = v; }

            opl* owner { nullptr };
            unsigned channel_num { };
            clock::time_point on_time { };
            clock::time_point off_time { };
        };
        using channel_2op = channel<2>;
        using channel_4op = channel<4>;

        opl(io::port_num port, opl_config c = { }) : basic_opl { port }, cfg { c } { update_config(); }
        virtual ~opl();

        void update();
        const opl_config& config() const noexcept { return cfg; }
        void config(const opl_config& c) { cfg = c; update_config(); };

        using base::read_setup;
        using base::read_timer;
        using base::read_4op;
        using base::read_percussion;
        using base::read_channel;
        using base::read_operator;
        using base::type;

    private:
        opl(const opl&) = delete;
        opl(opl&&) = delete;
        opl& operator=(const opl&) = delete;
        opl& operator=(opl&&) = delete;

        void update_config();
        template<unsigned N> void update(channel<N>* ch);
        template<unsigned N> void start(channel<N>* ch);
        template<unsigned N> void stop(channel<N>* ch);
        template<unsigned N> bool insert_at(std::uint8_t n, channel<N>* ch);
        template<unsigned N> bool insert(channel<N>*);
        template<unsigned N> void remove(channel<N>*) noexcept;
        template<unsigned N> void write(channel<N>*);
        template<unsigned N> void move(channel<N>*) noexcept;
        template<unsigned N> clock::time_point off_time(const channel<N>*, bool, clock::time_point) const noexcept;

        opl_config cfg { };
        std::array<channel_4op*, 6> channels_4op { };
        std::array<channel_2op*, 18> channels_2op { };
    };
}

namespace jw::audio::detail
{
    template<unsigned sample_rate>
    inline constexpr unsigned fnum(std::uint8_t blk, float f) noexcept
    {
        return static_cast<unsigned>(f * (1 << (20 - blk))) / sample_rate;
    }

    template<unsigned sample_rate>
    consteval long double fnum_to_freq(std::uint8_t blk, unsigned fnum) noexcept
    {
        return fnum * sample_rate / static_cast<long double>(1 << (20 - blk));
    }

    template<unsigned sample_rate>
    inline constexpr auto opl_freq(float freq) noexcept
    {
        constexpr unsigned max = fnum_to_freq<sample_rate>(7, 1023);
        constexpr std::uint8_t shift = std::bit_width(static_cast<unsigned>(fnum_to_freq<sample_rate>(0, 1023)));

        const auto f = static_cast<unsigned>(freq);
        std::uint8_t b = std::bit_width(f) - shift;
        b += (f << (7 - b)) > max;
        return std::make_tuple(fnum<sample_rate>(b, freq), b);
    }

    // Set fnum from MIDI note number, using a lookup table.
    template<unsigned sample_rate, long double A4>
    inline auto opl_note(int midi_note) noexcept
    {
        assume(midi_note < 128);
        constexpr std::uint8_t max_note = log2(fnum_to_freq<sample_rate>(0, 1023) / A4) * 12 + 69;
        constexpr std::uint8_t offset = max_note - 11;
        static constexpr auto scale = []
        {
            std::array<std::uint16_t, 12> array;
            for (int i = 0; i < 12; ++i)
                array[i] = fnum<sample_rate>(0, std::pow(2.0L, (i + offset - 69.0L) / 12.0L) * A4);
            return array;
        }();

        constexpr std::uint8_t adjust = 12 - (offset % 12);
        constexpr std::uint8_t adjust_div = (12 + offset) / 12;
        const unsigned n = midi_note + adjust;
        unsigned f = scale[n % 12];
        std::int8_t b = n / 12 - adjust_div;
        if (b < 0)
        {
            f >>= -b;
            b = 0;
        }
        return std::make_tuple(f, b);
    }

    // Set fnum from floating-point MIDI note.
    template<unsigned sample_rate, long double A4>
    inline constexpr auto opl_note(long double midi_note) noexcept
    {
        constexpr auto constant = 20 + log2(A4 / sample_rate);
        const auto exp = (midi_note - 69) / 12 + constant;
        const auto b = std::clamp(static_cast<int>(std::ceil(exp - log2(1023.L))), 0, 7);
        const auto f = round(__builtin_exp2l(exp - b));
        return std::make_tuple(std::min(static_cast<int>(f), 1023), b);
    }
}

namespace jw::audio
{
    template<unsigned sample_rate>
    inline constexpr void opl_channel::freq(float freq) noexcept
    {
        auto [f, b] = detail::opl_freq<sample_rate>(freq);
        freq_num = f;
        freq_block = b;
    }

    template<unsigned sample_rate, long double A4>
    inline void opl_channel::note(int midi_note) noexcept
    {
        auto [f, b] = detail::opl_note<sample_rate, A4>(midi_note);
        freq_num = f;
        freq_block = b;
    }

    template<unsigned sample_rate, long double A4>
    inline void opl_channel::note(long double midi_note) noexcept
    {
        auto [f, b] = detail::opl_note<sample_rate, A4>(midi_note);
        freq_num = f;
        freq_block = b;
    }

    inline void opl_channel::freq(const basic_opl& opl, float f) noexcept
    {
        if (opl.type() == opl_type::opl3_l) freq<49518>(f);
        else freq<49716>(f);
    }

    template<long double A4>
    inline void opl_channel::note(const basic_opl& opl, int midi_note) noexcept
    {
        if (opl.type() == opl_type::opl3_l) note<49518, A4>(midi_note);
        else note<49716, A4>(midi_note);
    }

    template<long double A4>
    inline void opl_channel::note(const basic_opl& opl, long double midi_note) noexcept
    {
        if (opl.type() == opl_type::opl3_l) note<49518, A4>(midi_note);
        else note<49716, A4>(midi_note);
    }

    inline void opl_channel::output(std::bitset<4> value) noexcept
    {
        auto* const p = reinterpret_cast<std::uint8_t*>(this);
        *p &= 0x0f;
        *p |= value.to_ulong() << 4;
    }

    inline constexpr std::bitset<4> opl_channel::output() const noexcept
    {
        return { static_cast<unsigned>(*reinterpret_cast<const std::uint8_t*>(this) >> 4) };
    }

    static_assert (sizeof(opl_status) == 1);
    static_assert (sizeof(opl_setup) == 4);
    static_assert (sizeof(opl_timer) == 3);
    static_assert (sizeof(opl_4op) == 1);
    static_assert (sizeof(opl_percussion) == 1);
    static_assert (sizeof(opl_operator) == 5);
    static_assert (sizeof(opl_channel) == 3);
}
