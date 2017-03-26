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
        dpmi::locked_pool_allocator<> alloc { 32_KB };
        std::deque<std::uint64_t, dpmi::locked_pool_allocator<>> samples { alloc };
        std::size_t tsc_sample_size;
        std::uint64_t tsc_total { 0 };
        std::uint64_t last_tsc;

        std::atomic<std::uint64_t> chrono::ps_per_tsc_tick;
        std::uint64_t chrono::ps_per_pit_tick;
        std::uint64_t chrono::ps_per_rtc_tick;

        std::atomic<std::uint64_t> chrono::pit_ticks;
        std::uint_fast16_t chrono::rtc_ticks;

        constexpr io::out_port<byte> chrono::rtc_index;
        constexpr io::io_port<byte> chrono::rtc_data;
        const io::out_port<byte> pit_cmd { 0x43 };
        const io::io_port<byte> pit0_data { 0x40 };

        void chrono::update_tsc()
        {
            auto tsc = rdtsc();
            auto diff = tsc - last_tsc;
            last_tsc = tsc;
            samples.push_back(diff);
            tsc_total += diff;
            while (samples.size() > tsc_sample_size)
            {
                tsc_total -= samples.front();
                samples.pop_front();
                std::cerr << "pop";
            }
            auto ps = rtc_irq.is_enabled() ? ps_per_rtc_tick : ps_per_pit_tick;
            ps_per_tsc_tick = ps / (tsc_total / samples.size());
            std::cerr << "tsc update, ps/tick = " << ps_per_tsc_tick << "\n";
        }

        dpmi::irq_handler chrono::rtc_irq { [](auto* ack)
        {
            try
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

            std::cerr << "rtc irq happened, count = " << rtc_ticks << "\n";

            update_tsc();

            ack();
            }
            catch (const std::exception& e) { std::cerr << e.what() << '\n'; }
        }, dpmi::always_call | dpmi::no_auto_eoi };

        dpmi::irq_handler chrono::pit_irq { [](auto* ack)
        {
            ++pit_ticks;
            std::cerr << "pit irq happened, count = " << pit_ticks << "\n";

            if (!rtc_irq.is_enabled()) update_tsc();

            ack();
        }, dpmi::always_call | dpmi::no_auto_eoi };

        void chrono::setup_pit(bool enable, std::uint32_t freq_divider)
        {
            dpmi::interrupt_mask no_irq { };
            reset_pit();
            if (!enable) return;

            ps_per_pit_tick = 1e12 / (max_pit_frequency / freq_divider);
            pit_irq.set_irq(1);
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

        void chrono::setup_tsc(std::size_t sample_size)
        {
            tsc_sample_size = sample_size;
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
            samples.clear();
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
            auto from_bcd = [](byte bcd)
            {
                return (bcd >> 4) * 10 + (bcd & 0xF);
            };

            std::uint64_t sec { 0 };
            chrono::rtc_index.write(0x80);  // second
            sec += from_bcd(chrono::rtc_data.read());
            chrono::rtc_index.write(0x82);  // minute
            sec += from_bcd(chrono::rtc_data.read()) * 60;
            chrono::rtc_index.write(0x84);  // hour
            sec += from_bcd(chrono::rtc_data.read()) * 60 * 60;
            chrono::rtc_index.write(0x87);  // day
            sec += from_bcd(chrono::rtc_data.read()) * 24 * 60 * 60;
            chrono::rtc_index.write(0x88);  // month
            sec += from_bcd(chrono::rtc_data.read()) * 24 * 60 * 60 * (365.2425 / 12);
            chrono::rtc_index.write(0x09);  // year
            sec += from_bcd(chrono::rtc_data.read()) * 24 * 60 * 60 * 365.2425;
            sec += 946684800;   // seconds from 1970 to 2000
            sec *= static_cast<std::uint64_t>(1e12);    // to picoseconds
            sec += chrono::rtc_ticks * chrono::ps_per_rtc_tick;
            sec /= static_cast<std::uint64_t>(1e6);    // to microseconds
            return time_point { duration{ sec } };
        }
    }
}
