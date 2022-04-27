/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <bit>
#include <jw/chrono.h>
#include <jw/thread.h>
#include <jw/io/ioport.h>
#include <jw/dpmi/irq_mask.h>
#include <jw/dpmi/cpu_exception.h>
#include <jw/dpmi/cpuid.h>

namespace jw::chrono
{
    static std::size_t tsc_max_sample_size { 1024 };
    static std::size_t tsc_max_sample_bits { 10 };
    static std::size_t tsc_sample_size { 0 };
    static std::uint64_t tsc_total { 0 };
    static bool tsc_resync { true };

    static std::atomic<std::uint32_t> tsc_ticks_per_irq { 0 };
    static constexpr fixed<std::uint32_t, 6> ns_per_pit_count { 1e9 / pit::max_frequency };
    static fixed<std::uint32_t, 6> ns_per_pit_tick;
    static double ns_per_rtc_tick;

    static std::uint32_t pit_counter_max;
    static volatile std::uint64_t pit_ticks;
    static volatile std::uint_fast16_t rtc_ticks;

    static timer_irq tsc_ref { timer_irq::none };

    static constexpr io::out_port<byte> rtc_index { 0x70 };
    static constexpr io::io_port<byte> rtc_data { 0x71 };
    static constexpr io::out_port<byte> pit_cmd { 0x43 };
    static constexpr io::io_port<byte> pit0_data { 0x40 };

    static void update_tsc()
    {
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

    static dpmi::irq_handler rtc_irq { []
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

        if (tsc_ref == timer_irq::rtc) update_tsc();

        dpmi::irq_handler::acknowledge();
    }, dpmi::always_call | dpmi::no_interrupts };

    static dpmi::irq_handler pit_irq { []
    {
        asm
        (
            "add %k0, 1;"
            "adc %k0+4, 0;"
            : "+m" (pit_ticks)
            :: "cc"
        );

        if (tsc_ref == timer_irq::pit) update_tsc();

        dpmi::irq_handler::acknowledge();
    }, dpmi::always_call | dpmi::no_auto_eoi };

    static void reset_tsc()
    {
        dpmi::interrupt_mask no_irq { };
        tsc_sample_size = 0;
        tsc_total = 0;
        tsc_ticks_per_irq = 0;
        tsc_resync = true;
    }

    static void reset_pit()
    {
        dpmi::interrupt_mask no_irq { };
        if (tsc_ref == timer_irq::pit)
        {
            reset_tsc();
            tsc_ref = timer_irq::none;
        }
        pit_irq.disable();
        pit_ticks = 0;
        pit_cmd.write(0x34);
        pit0_data.write(0);
        pit0_data.write(0);
        // todo: fix up DOS time at 0040:006C
    }

    static void reset_rtc()
    {
        dpmi::interrupt_mask no_irq { };
        if (tsc_ref == timer_irq::rtc)
        {
            reset_tsc();
            tsc_ref = timer_irq::none;
        }
        rtc_irq.disable();
        rtc_ticks = 0;
        rtc_index.write(0x8B);                  // disable NMI, select register 0x0B
        auto b = rtc_data.read();               // read register
        rtc_index.write(0x8B);
        rtc_data.write(b & ~0x40);              // clear interrupt enable bit
        rtc_index.write(0x0C);                  // enable NMI, select register 0x0C
        rtc_data.read();                        // read and discard data
    }

    void pit::setup(bool enable, std::uint32_t freq_divisor)
    {
        {
            dpmi::interrupt_mask no_irq { };
            reset_pit();
            if (not enable) return;

            if (freq_divisor < 1 or freq_divisor > 0x10000)
                throw std::out_of_range("PIT frequency divisor must be a value between 1 and 0x10000, inclusive.");

            pit_counter_max = freq_divisor;
            ns_per_pit_tick = 1e9 / (max_frequency / freq_divisor);
            pit_irq.set_irq(0);
            pit_irq.enable();

            split_uint16_t div { freq_divisor };
            pit_cmd.write(0b00'11'010'0); // select counter 0, write both lsb/msb, mode 2 (rate generator), binary mode
            pit0_data.write(div.lo);
            pit0_data.write(div.hi);
        }
        if (dpmi::interrupts_enabled())
        {
            const auto ticks = pit_ticks;
            do { } while (ticks == pit_ticks);
        }
    }

    void rtc::setup(bool enable, std::uint8_t freq_shift)
    {
        dpmi::interrupt_mask no_irq { };
        reset_rtc();
        if (not enable) return;

        if (freq_shift < 1 or freq_shift > 15)
            throw std::out_of_range { "RTC frequency shift must be a value between 1 and 15, inclusive." };

        ns_per_rtc_tick = 1e9 / (max_frequency >> (freq_shift - 1));
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

    void tsc::setup(timer_irq r, std::size_t sample_size)
    {
        if (not dpmi::cpuid::feature_flags().time_stamp_counter) return;

        auto tsc_ref_enabled = [](auto r)
        {
            switch (r)
            {
            case timer_irq::pit: return pit_irq.is_enabled();
            case timer_irq::rtc: return rtc_irq.is_enabled();
            default: return false;
            }
        };

        {
            dpmi::interrupt_mask no_irq { };
            if (sample_size != 0)
            {
                if (not std::has_single_bit(sample_size))
                    throw std::runtime_error { "TSC sample size must be a power of two." };
                tsc_max_sample_size = sample_size;
                tsc_max_sample_bits = std::bit_width(sample_size - 1);
            }

            if (tsc_ref_enabled(r))
            {
                tsc_ref = r;
                reset_tsc();
            }
        }

        const unsigned ticks = tsc_ticks_per_irq;
        if (dpmi::interrupts_enabled() and tsc_ref_enabled(tsc_ref))
            this_thread::yield_while([ticks] { return tsc_ticks_per_irq == ticks; });
    }

    rtc::time_point rtc::now() noexcept
    {
        auto from_bcd = [](byte bcd)
        {
            return (bcd >> 4) * 10 + (bcd & 0xF);
        };
        auto read = []
        {
            return rtc_data.read();
        };
        auto set_index = [](byte i)
        {
            rtc_index.write(i);
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
        usec += round(rtc_ticks * ns_per_rtc_tick / 1e3);
        return time_point { duration { usec } };
    }

    pit::time_point pit::now() noexcept
    {
        if (not pit_irq.is_enabled()) [[unlikely]]
        {
            auto t = std::chrono::duration_cast<duration>(std::chrono::steady_clock::now().time_since_epoch());
            return time_point { t };
        }
        std::uint64_t a, b;
        std::uint16_t counter;

        {
            dpmi::interrupt_mask no_irqs { };
            a = pit_ticks;
        }
        while (true)
        {
            asm ("nop");
            {
                dpmi::interrupt_mask no_irqs { };
                pit_cmd.write(0x00); // latch counter 0
                counter = split_uint16_t { pit0_data.read(), pit0_data.read() };
            }
            asm("nop");
            {
                dpmi::interrupt_mask no_irqs { };
                b = pit_ticks;
            }
            if (a == b) [[likely]] break;
            a = b;
        }
        auto nsec = ns_per_pit_tick * a;
        nsec += ns_per_pit_count * (pit_counter_max - counter);
        return time_point { duration { round(nsec) } };
    }

    tsc::time_point tsc::now() noexcept
    {
        if (tsc_ref == timer_irq::none) [[unlikely]]
        {
            return time_point { std::chrono::duration_cast<duration>(pit::now().time_since_epoch()) };
        }
        return to_time_point(rdtsc());
    }

    tsc::duration tsc::to_duration(tsc_count count)
    {
        double ns;
        switch (tsc_ref)
        {
        case timer_irq::pit: ns = ns_per_pit_tick; break;
        case timer_irq::rtc: ns = ns_per_rtc_tick; break;
        default: [[unlikely]] return duration::min();
        }
        ns *= count;
        ns /= tsc_ticks_per_irq;
        return duration { static_cast<std::uint64_t>(round(ns)) };
    }

    fixed<std::uint32_t, 6> pit::irq_delta() noexcept { return ns_per_pit_tick; }

    double rtc::irq_delta() noexcept { return ns_per_rtc_tick; }

    struct reset_all
    {
        ~reset_all()
        {
            reset_pit();
            reset_rtc();
        };
    } static reset;
}
