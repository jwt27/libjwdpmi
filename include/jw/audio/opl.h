/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <bitset>
#include <array>
#include <cstring>
#include <jw/io/ioport.h>
#include <jw/chrono/chrono.h>
#include <jw/thread/thread.h>
#include <../jwdpmi_config.h>

namespace jw::audio
{
    enum class opl_type
    {
        opl2, opl3, opl3_l
    };

    struct basic_opl final
    {
        using clock = config::thread_clock;

        struct [[gnu::packed]] common_registers
        {
            unsigned test : 8;
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
            void set_freq(float f)
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

            void set_freq(const basic_opl& opl, float f)
            {
                if (opl.type == opl_type::opl3_l) set_freq<49518>(f);
                else set_freq<49716>(f);
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
        ~basic_opl() { reset(); }

        void write(const common_registers& value);
        void write(const oscillator& value, std::uint8_t slot);
        void write(const oscillator& value, std::uint8_t ch, std::uint8_t osc) { write(value, oscillator_slot(ch, osc)); }
        void write(const channel& value, std::uint8_t ch);
        common_registers read() const noexcept { return common.value; }
        status_t status() const noexcept { return status_register.read(); }
        void reset();

        // Return absolute oscillator slot number for given operator in given channel.
        static constexpr std::uint8_t oscillator_slot(std::uint8_t ch, std::uint8_t osc) noexcept
        {
            return ch + 3 * (ch / 3) + 3 * osc;
        }

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
}
