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
#include <jw/dpmi/realmode.h>

namespace jw::chrono
{
    static std::size_t tsc_max_sample_size { 0 };
    static std::size_t tsc_max_sample_bits;
    static std::size_t tsc_sample_size { 0 };
    static std::uint64_t tsc_total { 0 };
    static bool tsc_resync { true };
    static std::uint64_t last_tsc;

    static std::uint32_t tsc_ticks_per_irq { 0 };
    static constexpr fixed<std::uint32_t, 6> ns_per_pit_count { 1e9 / pit::max_frequency };
    static fixed<std::uint32_t, 6> ns_per_pit_tick;
    static double ns_per_rtc_tick;

    static constexpr std::uint64_t pit_ns_offset { 1'640'991'600'000'000'000ull }; // 2022-01-01 UNIX time in nanoseconds
    static fixed<std::uint64_t, 6> pit_ns;
    static std::uint32_t pit_counter_max;
    static volatile std::uint_fast16_t rtc_ticks;

    static constexpr io::out_port<byte> rtc_index { 0x70 };
    static constexpr io::io_port<byte> rtc_data { 0x71 };
    static constexpr io::out_port<byte> pit_cmd { 0x43 };
    static constexpr io::io_port<byte> pit0_data { 0x40 };

    struct rtc_time
    {
        std::uint8_t year, month, day, hour, min, sec;
    };

    static rtc_time read_rtc() noexcept
    {
        auto from_bcd = [](std::uint8_t bcd) -> std::uint8_t
        {
            return (bcd >> 4) * 10 + (bcd & 0xF);
        };

        std::uint8_t year, month, day, hour, min, sec;  // BCD

        {
            dpmi::interrupt_mask no_irq { };
            rtc_index.write(0x80);
            sec = rtc_data.read();
            rtc_index.write(0x82);
            min = rtc_data.read();
            rtc_index.write(0x84);
            hour = rtc_data.read();
            rtc_index.write(0x87);
            day = rtc_data.read();
            rtc_index.write(0x88);
            month = rtc_data.read();
            rtc_index.write(0x09);
            year = rtc_data.read();
        }

        return
        {
            from_bcd(year),
            from_bcd(month),
            from_bcd(day),
            from_bcd(hour),
            from_bcd(min),
            from_bcd(sec)
        };
    }

    static void update_tsc()
    {
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

        dpmi::irq_handler::acknowledge();
    }, dpmi::always_call | dpmi::no_interrupts };

    static dpmi::irq_handler pit_irq { []
    {
        auto t = pit_ns;
        t += ns_per_pit_tick;
        volatile_store(&pit_ns.value, t.value);

        if (tsc_max_sample_size != 0) [[likely]] update_tsc();

        dpmi::irq_handler::acknowledge();
    }, dpmi::always_call | dpmi::no_auto_eoi };

    static void reset_tsc()
    {
        tsc_sample_size = 0;
        tsc_total = 0;
        tsc_ticks_per_irq = 0;
        tsc_resync = true;
    }

    static void reset_pit()
    {
        if (not pit_irq.is_enabled()) return;
        pit_irq.disable();
        pit_cmd.write(0x34);
        pit0_data.write(0);
        pit0_data.write(0);

        const auto t = read_rtc();
        dpmi::realmode_registers reg { };

        reg.ah = 0x2b;      // Set date
        reg.cx = t.year + 2000;
        reg.dh = t.month;
        reg.dl = t.day;
        reg.call_int(0x21);

        reg.ss = reg.sp = 0;

        reg.ah = 0x2d;      // Set time
        reg.ch = t.hour;
        reg.cl = t.min;
        reg.dh = t.sec;
        reg.dl = 0;
        reg.call_int(0x21);
    }

    static void reset_rtc()
    {
        if (not rtc_irq.is_enabled()) return;
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
            if (pit_irq.is_enabled() and not enable)
            {
                reset_pit();
                return;
            }
            else if (not pit_irq.is_enabled() and enable)
            {
                const auto t = std::chrono::steady_clock::now();
                pit_ns = t.time_since_epoch().count() - pit_ns_offset;
                pit_irq.set_irq(0);
            }

            if (freq_divisor < 1 or freq_divisor > 0x10000)
                throw std::out_of_range("PIT frequency divisor must be a value between 1 and 0x10000, inclusive.");

            pit_counter_max = freq_divisor;
            ns_per_pit_tick = 1e9 / (max_frequency / freq_divisor);
            pit_irq.enable();

            split_uint16_t div { freq_divisor };
            pit_cmd.write(0b00'11'010'0); // select counter 0, write both lsb/msb, mode 2 (rate generator), binary mode
            pit0_data.write(div.lo);
            pit0_data.write(div.hi);
        }
        if (dpmi::interrupts_enabled())
        {
            const auto t = pit_ns;
            do { } while (t.value == volatile_load(&pit_ns.value));
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

    void tsc::setup(std::size_t sample_size)
    {
        if (not dpmi::cpuid::feature_flags().time_stamp_counter) return;

        {
            dpmi::interrupt_mask no_irq { };
            if (sample_size != 0)
            {
                if (not std::has_single_bit(sample_size))
                    throw std::runtime_error { "TSC sample size must be a power of two." };
                tsc_max_sample_bits = std::bit_width(sample_size - 1);
            }
            tsc_max_sample_size = sample_size;
            reset_tsc();
        }

        const unsigned ticks = tsc_ticks_per_irq;
        if (tsc_max_sample_size != 0 and dpmi::interrupts_enabled() and pit_irq.is_enabled())
            this_thread::yield_while([ticks] { return volatile_load(&tsc_ticks_per_irq) == ticks; });
    }

    pit::time_point pit::now() noexcept
    {
        if (not pit_irq.is_enabled()) [[unlikely]]
            return time_point { std::chrono::steady_clock::now().time_since_epoch() };

        decltype(pit_ns) a, b;
        std::uint16_t counter;

        {
            dpmi::interrupt_mask no_irqs { };
            a.value = volatile_load(&pit_ns.value);
        }
        while (true)
        {
            asm ("nop");
            {
                dpmi::interrupt_mask no_irqs { };
                pit_cmd.write(0x00); // latch counter 0
                counter = split_uint16_t { pit0_data.read(), pit0_data.read() };
            }
            asm ("nop");
            {
                dpmi::interrupt_mask no_irqs { };
                b.value = volatile_load(&pit_ns.value);
            }
            if (a.value == b.value) [[likely]] break;
            a = b;
        }
        a += ns_per_pit_count * (pit_counter_max - counter);
        return time_point { duration { round(a) + pit_ns_offset } };
    }

    fixed<std::uint32_t, 6> pit::irq_delta() noexcept { return ns_per_pit_tick; }

    rtc::time_point rtc::now() noexcept
    {
        const auto t = read_rtc();
        unsigned y = t.year + 2000;
        const unsigned m = t.month;
        const unsigned d = t.day;

        // algorithm from http://howardhinnant.github.io/date_algorithms.html
        y -= m <= 2;
        const unsigned era = y / 400;
        const unsigned yoe = y - era * 400;
        const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        const unsigned days_since_1970 = era * 146097 + doe - 719468;

        std::uint64_t unix_time = days_since_1970 * 60 * 60 * 24;
        unix_time += t.hour * 60 * 60;
        unix_time += t.min * 60;
        unix_time += t.sec;

        std::uint64_t usec = unix_time * 1'000'000;
        usec += round(rtc_ticks * ns_per_rtc_tick / 1e3);
        return time_point { duration { usec } };
    }

    double rtc::irq_delta() noexcept { return ns_per_rtc_tick; }

    static long double tsc_to_ns(tsc_count count)
    {
        long double ns = ns_per_pit_tick;
        ns /= tsc_ticks_per_irq;
        ns *= count;
        return ns;
    }

    tsc::time_point tsc::now() noexcept
    {
        if (tsc_max_sample_size == 0) [[unlikely]]
            return time_point { pit::now().time_since_epoch() };

        decltype(pit_ns) pit;
        tsc_count last;

        {
            dpmi::interrupt_mask no_irqs { };
            pit.value = volatile_load(&pit_ns.value);
            last = volatile_load(&last_tsc);
        }

        const auto ns = pit + tsc_to_ns(rdtsc() - last);
        return time_point { duration { static_cast<std::int64_t>(round(ns)) } };
    }

    tsc::duration tsc::to_duration(tsc_count count)
    {
        return duration { static_cast<std::int64_t>(round(tsc_to_ns(count))) };
    }

    struct reset_all
    {
        ~reset_all()
        {
            dpmi::interrupt_mask no_irq { };
            reset_pit();
            reset_rtc();
        };
    } reset;
}
