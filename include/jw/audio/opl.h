/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */

#pragma once

#include <bitset>
#include <array>
#include <jw/io/ioport.h>
#include <jw/chrono/chrono.h>
#include <../jwdpmi_config.h>

namespace jw::audio
{
    enum class opl_type
    {
        opl2, opl3, opl3_l
    };

    struct basic_opl
    {
        using clock = config::midi_clock;

        struct [[gnu::packed]] common_registers
        {
            unsigned test0 : 5;
            bool enable_waveform_select : 1;    // OPL2 only
            unsigned test1 : 2;
            unsigned timer0 : 8;
            unsigned timer1 : 8;
            bool start_timer0 : 1;
            bool start_timer1 : 1;
            unsigned : 3;
            bool mask_timer1 : 1;
            bool mask_timer0 : 1;
            bool reset_irq : 1;
            unsigned : 6;
            bool note_sel : 1;
            bool enable_composite_sine_mode : 1;
            bool hihat : 1;
            bool top_cymbal : 1;
            bool tomtom : 1;
            bool snare_drum : 1;
            bool bass_drum : 1;
            bool enable_percussion : 1;
            unsigned vibrato_depth : 1;
            unsigned tremolo_depth : 1;
            unsigned test_opl3 : 6;
            unsigned : 2;
            struct [[gnu::packed]]
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
            } enable_4op;
            bool enable_opl3 : 1;
            unsigned : 1;
            bool enable_opl3_l : 1;
            unsigned : 5;
        };
        static_assert(sizeof(common_registers) == 9);

        struct [[gnu::packed]] oscillator
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
        static_assert(sizeof(oscillator) == 5);

        struct [[gnu::packed]] channel
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
            constexpr void freq(float f)
            {
                constexpr auto calc_blocks = []
                {
                    std::array<unsigned, 8> result;
                    for (int i = 0; i < 8; i++) result[i] = 1023 * sample_rate / (1 << (20 - i));
                    return result;
                };
                constexpr std::array<unsigned, 8> block_maxfreq { calc_blocks() };

                unsigned i;
                for (i = 0; i < 8; ++i)
                    if (f <= block_maxfreq[i]) break;

                freq_block = i;
                freq_num = static_cast<unsigned>(f * (1 << (20 - i))) / sample_rate;
            }

            constexpr void freq(const basic_opl& opl, float f)
            {
                if (opl.type == opl_type::opl3_l) freq<49518>(f);
                else freq<49716>(f);
            }

            void output(std::bitset<4> value) noexcept
            {
                auto* const p = reinterpret_cast<std::uint8_t*>(this);
                *p &= 0x0f;
                *p |= value.to_ulong() << 4;
            }
            constexpr std::bitset<4> output() const noexcept
            {
                return { static_cast<unsigned>(*reinterpret_cast<const std::uint8_t*>(this) >> 4) };
            }
        };
        static_assert(sizeof(channel) == 3);

        struct [[gnu::packed]] status_t
        {
            bool busy : 1;
            bool opl2 : 1;
            bool busy2 : 1;
            unsigned : 2;
            bool timer1 : 1;
            bool timer0 : 1;
            bool irq : 1;
        };
        static_assert(sizeof(status_t) == 1);

        basic_opl(io::port_num port);
        virtual ~basic_opl() { reset(); }

        void write(const common_registers& value);
        void write(const oscillator& value, std::uint8_t slot);
        void write(const oscillator& value, std::uint8_t ch, std::uint8_t osc) { write(value, oscillator_slot(ch, osc)); }
        void write(const channel& value, std::uint8_t ch);
        const common_registers& read() const noexcept { return common.value; }
        status_t status() const noexcept { return status_register.read(); }
        bool is_4op(std::uint8_t ch_4op) const noexcept { return read().enable_4op.bitset()[ch_4op]; };
        void set_4op(std::uint8_t ch_4op, bool enable);
        void reset();

        // Returns absolute oscillator slot number for given operator in given channel.
        static constexpr std::uint8_t oscillator_slot(std::uint8_t ch, std::uint8_t osc) noexcept
        {
            return ch + 3 * (ch / 3) + 3 * osc;
        }

        // Returns primary 2op channel number for given 4op channel number.
        static constexpr std::uint8_t lookup_4to2_pri(std::uint8_t ch_4op) noexcept { return table_4to2[ch_4op]; }

        // Returns secondary 2op channel number for given 4op channel number.
        static constexpr std::uint8_t lookup_4to2_sec(std::uint8_t ch_4op) noexcept { return table_4to2[ch_4op] + 3; }

        // Returns 4op channel number that the given 2op channel is part of, or 0xff if none.
        static constexpr std::uint8_t lookup_2to4(std::uint8_t ch_2op) noexcept { return table_2to4[ch_2op]; }

    protected:
        const channel& read_channel(std::uint8_t ch) const noexcept { return channels[ch].value; }
        const oscillator& read_oscillator(std::uint8_t osc) const noexcept { return oscillators[osc].value; }
        const oscillator& read_oscillator(std::uint8_t ch, std::uint8_t osc) const noexcept { return read_oscillator(oscillator_slot(ch, osc)); }

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

        template <typename T>
        struct cached_reg : reg<T>
        {
            std::bitset<sizeof(T)> written { };
        };

        template<unsigned... R, typename T> void write(const T& v, cached_reg<T>& cache, unsigned offset);
        template<opl_type t> void do_write(std::uint16_t reg, std::byte value);
        void write(std::uint16_t reg, std::byte value);
        opl_type detect();

        static constexpr std::uint8_t table_4to2[] { 0, 1, 2, 9, 10, 11 };
        static constexpr std::uint8_t table_2to4[] { 0, 1, 2, 0, 1, 2, 0xff, 0xff, 0xff,
                                                     3, 4, 5, 3, 4, 5, 0xff, 0xff, 0xff };

        cached_reg<common_registers> common { };
        std::array<cached_reg<oscillator>, 36> oscillators { };
        std::array<cached_reg<channel>, 18> channels { };
        clock::time_point last_access { };
        io::in_port<status_t> status_register;
        std::array<io::out_port<std::uint8_t>, 2> index;
        std::array<std::uint8_t, 2> current_index;
        std::array<io::io_port<std::byte>, 2> data;

    public:
        const opl_type type;
    };

    struct opl_config
    {
        // Try to allocate 2op channels so that they don't overlap with 4op channels.  Has no effect for OPL2.
        // In automatic mode, this setting is activated only if there are any active 4op channels.
        enum : std::uint8_t { no, yes, automatic } prioritize_4op { automatic };

        // Ignore channel priority field.
        bool ignore_priority { false };

        // Determines if key scale rate/level is calculated by highest or second highest bit in freq_num.
        bool note_select { false };

        // For tremolo: low = 1dB, high = 4.8dB.  For vibrato: low = 7%, high = 14%.
        enum : unsigned { low, high } tremolo_depth : 1 { low }, vibrato_depth : 1 { low };
    };

    struct opl final : private basic_opl
    {
        using base = basic_opl;

        template<unsigned N>
        struct channel final : basic_opl::channel
        {
            static_assert(N == 2 or N == 4);
            using base = basic_opl::channel;
            friend struct opl;

            std::bitset<N/2> connection { 0b00 };
            std::array<basic_opl::oscillator, N> osc { };
            int priority { 0 };

            constexpr channel() noexcept = default;
            ~channel() { if (owner != nullptr) owner->remove(this); }

            channel(const channel&) noexcept;
            channel& operator=(const channel&) noexcept;
            channel(channel&& c) noexcept;
            channel& operator=(channel&& c) noexcept;

            void freq(const opl& o, float f) noexcept { base::freq(o, f); }
            bool play(opl& o) { return o.insert(this); }
            bool playing() const noexcept { return owner != nullptr; }
            void update() { if (owner != nullptr) owner->update(this); }
            void stop() { if (owner != nullptr) owner->stop(this); }
            bool retrigger(opl& o) { return o.retrigger(this); }

        private:
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

    private:
        opl(const opl&) = delete;
        opl(opl&&) = delete;
        opl& operator=(const opl&) = delete;
        opl& operator=(opl&&) = delete;

        void update_config();
        template<unsigned N> void update(channel<N>* ch);
        template<unsigned N> void stop(channel<N>* ch);
        template<unsigned N> bool retrigger(channel<N>* ch);
        template<unsigned N> bool insert_at(std::uint8_t n, channel<N>* ch);
        template<unsigned N> bool insert(channel<N>*);
        template<unsigned N> void remove(channel<N>*) noexcept;
        template<unsigned N> void write(channel<N>*);
        template<unsigned N> void move(channel<N>*) noexcept;

        opl_config cfg { };
        std::array<channel_4op*, 6> channels_4op { };
        std::array<channel_2op*, 18> channels_2op { };
    };
}
