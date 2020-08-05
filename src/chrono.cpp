/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <bit>
#include <jw/chrono/chrono.h>
#include <jw/io/ioport.h>
#include <jw/dpmi/irq_mask.h>
#include <jw/dpmi/cpu_exception.h>
#include <jw/dpmi/cpuid.h>

namespace jw
{
    namespace chrono
    {
        std::size_t tsc_max_sample_size { 1024 };
        std::size_t tsc_max_sample_bits { 10 };
        std::size_t tsc_sample_size { 0 };
        std::uint64_t tsc_total { 0 };
        bool tsc_resync { true };

        void setup::update_tsc()
        {
            if (not have_rdtsc) [[unlikely]] return;
            static std::uint64_t last_tsc;
            auto tsc = rdtsc();
            std::uint32_t diff = tsc - last_tsc;
            last_tsc = tsc;
            if (tsc_resync) [[unlikely]] { tsc_resync = false; return; }
            if (tsc_sample_size == tsc_max_sample_size)
            {
                tsc_total -= static_cast<std::uint32_t>(tsc_total >> tsc_max_sample_bits);
                --tsc_sample_size;
            }
            tsc_total += diff;
            ++tsc_sample_size;
            if (tsc_sample_size == tsc_max_sample_size) [[likely]]
                tsc_ticks_per_irq = tsc_total >> tsc_max_sample_bits;
            else
                tsc_ticks_per_irq = tsc_total / tsc_sample_size;
        }

        dpmi::irq_handler setup::rtc_irq { []
        {
            static byte last_sec { 0 };

            rtc_index.write(0x80);
            auto sec = rtc_data.read();
            if (sec != last_sec) [[unlikely]]
            {
                rtc_ticks = 0;
                last_sec = sec;
            }
            else asm ("inc %0" : "+m" (rtc_ticks));

            rtc_index.write(0x0C);
            rtc_data.read();

            if (current_tsc_ref() == tsc_reference::rtc) update_tsc();

            dpmi::irq_handler::acknowledge();
        }, dpmi::always_call | dpmi::no_interrupts };

        dpmi::irq_handler setup::pit_irq { []
        {
            asm
            (
                "add %k0, 1;"
                "adc %k0+4, 0;"
                : "+m" (pit_ticks)
                :: "cc"
            );

            if (current_tsc_ref() == tsc_reference::pit) update_tsc();

            dpmi::irq_handler::acknowledge();
        }, dpmi::always_call | dpmi::no_auto_eoi };

        void setup::setup_pit(bool enable, std::uint32_t freq_divisor)
        {
            dpmi::interrupt_mask no_irq { };
            reset_pit();
            if (!enable) return;

            if (freq_divisor < 1 || freq_divisor > 0x10000)
                throw std::out_of_range("PIT frequency divisor must be a value between 1 and 0x10000, inclusive.");

            pit_counter_max = freq_divisor;
            ns_per_pit_tick = 1e9 / (max_pit_frequency / freq_divisor);
            pit_irq.set_irq(0);
            pit_irq.enable();

            split_uint16_t div { freq_divisor };
            pit_cmd.write(0b00'11'010'0); // select counter 0, write both lsb/msb, mode 2 (rate generator), binary mode
            pit0_data.write(div.lo);
            pit0_data.write(div.hi);
        }

        void setup::setup_rtc(bool enable, std::uint8_t freq_shift)
        {
            dpmi::interrupt_mask no_irq { };
            reset_rtc();
            if (!enable) return;

            if (freq_shift < 1 || freq_shift > 15) throw std::out_of_range("RTC frequency shift must be a value between 1 and 15, inclusive.");
            ns_per_rtc_tick = 1e9 / (max_rtc_frequency >> (freq_shift - 1));
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

        void setup::setup_tsc(std::size_t sample_size, tsc_reference r)
        {
            have_rdtsc = dpmi::cpuid::feature_flags().time_stamp_counter;
            if (sample_size != 0)
            {
                if (not std::has_single_bit(sample_size)) throw std::runtime_error { "Number of TSC samples must be a power of two." };
                tsc_max_sample_size = sample_size;
                tsc_max_sample_bits = std::bit_width(sample_size - 1);
            }
            if (r != tsc_reference::none and (r != current_tsc_ref() or current_tsc_ref() == tsc_reference::none))
            {
                preferred_tsc_ref = r;
                reset_tsc();
            }
            if (have_rdtsc) thread::yield_while([] { return tsc_ticks_per_irq == 0; });
        }

        void setup::reset_pit()
        {
            dpmi::interrupt_mask no_irq { };
            if (current_tsc_ref() == tsc_reference::pit) reset_tsc();
            pit_irq.disable();
            pit_ticks = 0;
            pit_cmd.write(0x34);
            pit0_data.write(0);
            pit0_data.write(0);
            // todo: fix up DOS time at 0040:006C
        }

        void setup::reset_rtc()
        {
            dpmi::interrupt_mask no_irq { };
            if (current_tsc_ref() == tsc_reference::rtc) reset_tsc();
            rtc_irq.disable();
            rtc_ticks = 0;
            rtc_index.write(0x8B);                  // disable NMI, select register 0x0B
            auto b = rtc_data.read();               // read register
            rtc_index.write(0x8B);
            rtc_data.write(b & ~0x40);              // clear interrupt enable bit
            rtc_index.write(0x0C);                  // enable NMI, select register 0x0C
            rtc_data.read();                        // read and discard data
        }

        void setup::reset_tsc()
        {
            dpmi::interrupt_mask no_irq { };
            tsc_sample_size = 0;
            tsc_total = 0;
            tsc_ticks_per_irq = 0;
            tsc_resync = true;
        }

        setup::reset_all::~reset_all()
        {
            reset_pit();
            reset_rtc();
        }

        rtc::time_point rtc::now() noexcept
        {
            auto from_bcd = [](byte bcd)
            {
                return (bcd >> 4) * 10 + (bcd & 0xF);
            };
            auto read = []
            {
                return setup::rtc_data.read();
            };
            auto set_index = [](byte i)
            {
                setup::rtc_index.write(i);
            };

            byte year, month, day, hour, min, sec;  // BCD

            {
                dpmi::interrupt_mask no_irq { };
                set_index(0x80);    // second
                sec = read();
                set_index(0x82);    // minute
                min = read();
                set_index(0x84);    // hour
                hour = read();
                set_index(0x87);    // day
                day = read();
                set_index(0x88);    // month
                month = read();
                set_index(0x09);    // year
                year = read();
            }

            unsigned y = 2000 + from_bcd(year);
            const unsigned m = from_bcd(month);
            const unsigned d = from_bcd(day);

            // algorithm from http://howardhinnant.github.io/date_algorithms.html
            y -= m <= 2;
            const unsigned era = y / 400;
            const unsigned yoe = y - era * 400;
            const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
            const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            const unsigned days_since_1970 = era * 146097 + doe - 719468;

            std::uint64_t unix_time = days_since_1970 * 60 * 60 * 24;
            unix_time += from_bcd(hour) * 60 * 60;
            unix_time += from_bcd(min) * 60;
            unix_time += from_bcd(sec);

            std::uint64_t usec = unix_time * static_cast<std::uint64_t>(1e6);
            usec += jw::round(setup::rtc_ticks * setup::ns_per_rtc_tick / 1e3);
            return time_point { duration { usec } };
        }
    }
}
