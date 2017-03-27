/******************************* libjwdpmi **********************************
Copyright (C) 2016-2017  J.W. Jagersma

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <jw/chrono/chrono.h>
#include <jw/io/ioport.h>
#include <jw/dpmi/irq_mask.h>

namespace jw
{
    namespace chrono
    {
        std::size_t tsc_max_sample_size { 1024 };
        std::size_t tsc_sample_size { 0 };
        std::uint64_t tsc_total { 0 };
        std::uint64_t last_tsc;
        bool sync_tsc_to_rtc { true };

        std::atomic<std::uint64_t> chrono::ps_per_tsc_tick;
        std::uint64_t chrono::ps_per_pit_tick;
        std::uint64_t chrono::ps_per_rtc_tick;

        std::atomic<std::uint64_t> chrono::pit_ticks;
        std::uint_fast16_t chrono::rtc_ticks;

        constexpr io::out_port<byte> chrono::rtc_index;
        constexpr io::io_port<byte> chrono::rtc_data;
        const io::out_port<byte> pit_cmd { 0x43 };
        const io::io_port<byte> pit0_data { 0x40 };

        chrono::reset_all chrono::reset;

        void chrono::update_tsc()
        {
            auto tsc = rdtsc();
            auto diff = tsc - last_tsc;
            last_tsc = tsc;
            tsc_total += diff;
            ++tsc_sample_size;
            while (tsc_sample_size > tsc_max_sample_size)
            {
                tsc_total -= (tsc_total / tsc_sample_size);
                --tsc_sample_size;
            }
            auto ps = (sync_tsc_to_rtc && rtc_irq.is_enabled()) ? ps_per_rtc_tick : ps_per_pit_tick;
            ps_per_tsc_tick = ps * tsc_sample_size / tsc_total;
        }

        dpmi::irq_handler chrono::rtc_irq { [](auto* ack) INTERRUPT
        {
            static byte last_sec { 0 };
            dpmi::interrupt_mask no_irq { };

            rtc_index.write(0x80);
            auto sec = rtc_data.read();
            if (sec != last_sec)
            {
                rtc_ticks = 0;
                last_sec = sec;
            }
            else ++rtc_ticks;

            rtc_index.write(0x0C);
            rtc_data.read();

            if (sync_tsc_to_rtc || !pit_irq.is_enabled()) update_tsc();

            ack();
        }, dpmi::always_call };

        dpmi::irq_handler chrono::pit_irq { [](auto* ack) INTERRUPT
        {
            ++pit_ticks;

            if (!sync_tsc_to_rtc || !rtc_irq.is_enabled()) update_tsc();

            ack();
        }, dpmi::always_call };

        void chrono::setup_pit(bool enable, std::uint32_t freq_divider)
        {
            dpmi::interrupt_mask no_irq { };
            reset_pit();
            if (!enable) return;

            if (freq_divider < 1 || freq_divider > 0x10000) 
                throw std::out_of_range("PIT frequency divisor must be a value between 1 and 0x10000, inclusive.");
            ps_per_pit_tick = 1e12 / (max_pit_frequency / freq_divider);
            pit_irq.set_irq(0);
            pit_irq.enable();

            split_uint16_t div { freq_divider };
            pit_cmd.write(0x34);
            pit0_data.write(div.lo);
            pit0_data.write(div.hi);
        }

        void chrono::setup_rtc(bool enable, std::uint8_t freq_shift)
        {
            dpmi::interrupt_mask no_irq { };
            reset_rtc();
            if (!enable) return;

            if (freq_shift > 15) throw std::out_of_range("RTC frequency shift must be a value between 0 and 15, inclusive.");
            ps_per_rtc_tick = 1e12 / (max_rtc_frequency >> (freq_shift - 1));
            rtc_irq.set_irq(8);
            rtc_irq.enable();

            rtc_index.write(0x8B);                  // disable NMI, select register 0x0B
            auto b = rtc_data.read();               // read register
            rtc_index.write(0x8B);
            rtc_data.write(b | 0x40);               // set interrupt enable bit

            freq_shift &= 0x0F;
            rtc_index.write(0x8A);                  // disable NMI, select register 0x0A
            auto a = rtc_data.read() & 0xF0;        // read register, clear lower 4 bits
            rtc_index.write(0x8A);
            rtc_data.write(a | freq_shift);         // set freq shift bits

            rtc_index.write(0x0C);                  // enable NMI, select register 0x0C
            rtc_data.read();                        // read and discard data
        }

        void chrono::setup_tsc(std::size_t sample_size, bool use_rtc)
        {
            if (sample_size == 0) throw std::out_of_range("TSC sample size must be non-zero.");
            tsc_max_sample_size = sample_size;
            if (use_rtc != sync_tsc_to_rtc)
            {
                sync_tsc_to_rtc = use_rtc;
                reset_tsc();
            }
        }

        void chrono::reset_pit()
        {
            dpmi::interrupt_mask no_irq { };
            pit_irq.disable();
            pit_ticks = 0;
            reset_tsc();
            pit_cmd.write(0x34);
            pit0_data.write(0);
            pit0_data.write(0);
        }

        void chrono::reset_rtc()
        {
            dpmi::interrupt_mask no_irq { };
            rtc_irq.disable();
            rtc_ticks = 0;
            reset_tsc();
            rtc_index.write(0x8B);                  // disable NMI, select register 0x0B
            auto b = rtc_data.read();               // read register
            rtc_index.write(0x8B);
            rtc_data.write(b & ~0x40);              // clear interrupt enable bit
            rtc_index.write(0x0C);                  // enable NMI, select register 0x0C
            rtc_data.read();                        // read and discard data
        }

        void chrono::reset_tsc()
        {
            dpmi::interrupt_mask no_irq { };
            tsc_sample_size = 0;
            tsc_total = 0;
            last_tsc = rdtsc();
        }

        chrono::reset_all::~reset_all()
        {
            reset_pit();
            reset_rtc();
        }

        rtc::time_point rtc::now() noexcept
        {
            dpmi::interrupt_mask no_irq { };
            auto read_bcd = []
            {
                auto bcd = chrono::rtc_data.read();
                return (bcd >> 4) * 10 + (bcd & 0xF);
            };
            auto set_index = [](byte i)
            {
                chrono::rtc_index.write(i);
            };

            std::uint64_t sec { 0 };
            set_index(0x80);    // second
            sec += read_bcd();
            set_index(0x82);    // minute
            sec += read_bcd() * 60;
            set_index(0x84);    // hour
            sec += read_bcd() * 60 * 60;
            set_index(0x87);    // day
            sec += (read_bcd() - 1) * 60 * 60 * 24;
            set_index(0x88);    // month
            sec += (read_bcd() - 1) * 60 * 60 * 24 * (365.2425 / 12);
            set_index(0x09);    // year
            sec += read_bcd() * 60 * 60 * 24 * 365.2425;
            sec += 946684800;   // seconds from 1970 to 2000
            sec *= static_cast<std::uint64_t>(1e6);
            sec += chrono::rtc_ticks * chrono::ps_per_rtc_tick / 1e6;
            return time_point { duration { sec } };
        }
    }
}
