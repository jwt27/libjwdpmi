/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2023 J.W. Jagersma, see COPYING.txt for details */
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
#include <jw/dpmi/bda.h>

namespace jw::chrono
{
    static std::uint32_t last_tsc;
    static constinit bool tsc_calibrated { false };
    static volatile bool wait_for_irq0 { false };

    static constexpr fixed<std::uint32_t, 22> ns_per_pit_count { 1e9L / pit::max_frequency };
    static fixed<std::uint32_t, 6> ns_per_pit_tick { 0x10000 * ns_per_pit_count  };
    static double ns_per_rtc_tick;
    static fixed<std::uint32_t, 24> fixed_ns_per_tsc_tick;
    static long double float_ns_per_tsc_tick;
    static long double cpu_freq;

    static constexpr std::uint64_t pit_ns_offset { 1'640'995'200'000'000'000ull }; // 2022-01-01 UNIX time in nanoseconds
    static fixed<std::uint64_t, 6> pit_ns;
    static std::uint32_t pit_bios_count { 0 };
    static std::uint32_t pit_counter_max { 0x10000 };
    static std::uint32_t pit_counter_new_max;
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

    static void recalculate_pit_interval(std::uint32_t count) noexcept
    {
        ns_per_pit_tick = round_to<6>(count * ns_per_pit_count);
        pit_counter_max = count;
    }

    template<bool tsc>
    [[gnu::hot]] static void irq0()
    {
        if constexpr (tsc) last_tsc = rdtsc();
        pit_ns += ns_per_pit_tick;
        pit_bios_count += pit_counter_max;
        if (pit_bios_count > 0xffff) [[likely]]
        {
            pit_bios_count &= 0xffff;

            constexpr auto ticks_per_day { static_cast<std::uint32_t>(round(24 * 60 * 60 * (pit::max_frequency / 0x10000))) };
            auto bios_time = dpmi::bda->read<std::uint32_t>(0x6c);
            if (++bios_time >= ticks_per_day) [[unlikely]]
            {
                bios_time = 0;
                const auto day = dpmi::bda->read<std::uint8_t>(0x70);
                dpmi::bda->write<std::uint8_t>(0x70, day + 1);              // Update BIOS day counter
            }
            dpmi::bda->write(0x6c, bios_time);                              // Update BIOS timer

            auto motor_enable = dpmi::bda->read<std::uint8_t>(0x40);
            if (motor_enable > 0)
            {
                if (--motor_enable == 0)
                {
                    // Turn off floppy drive motors and update status bits.
                    io::write_port<std::uint8_t>(0x3f2, 0x0c);
                    const auto status = dpmi::bda->read<std::uint8_t>(0x3f);
                    dpmi::bda->write<std::uint8_t>(0x3f, status & 0xf0);
                }
                dpmi::bda->write(0x40, motor_enable);
            }
        }

        if (pit_counter_max != pit_counter_new_max) [[unlikely]]
        {
            // When a new count value is programmed, the PIT only loads it
            // after the current counting cycle is finished.
            recalculate_pit_interval(pit_counter_new_max);
        }

        wait_for_irq0 = false;
        dpmi::irq_handler::acknowledge<0>();
    }

    [[gnu::hot]] static void irq8()
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

        dpmi::irq_handler::acknowledge<8>();
    }

    static dpmi::irq_handler pit_irq { 0, [] { }, dpmi::always_call | dpmi::no_auto_eoi };
    static dpmi::irq_handler rtc_irq { 8, [] { irq8(); }, dpmi::always_call | dpmi::no_interrupts };

    static void select_irq0_handler()
    {
        if (tsc_calibrated) pit_irq = [] { irq0<true>(); };
        else pit_irq = [] { irq0<false>(); };
    }

    static void write_pit(split_uint16_t count) noexcept
    {
        pit_cmd.write(0b00'11'010'0); // select counter 0, write both lsb/msb, mode 2 (rate generator), binary mode
        pit0_data.write(count.lo);
        pit0_data.write(count.hi);
    }

    static void do_pit_reset() noexcept
    {
        pit_bios_count = 0;
        write_pit(0x10000);
        recalculate_pit_interval(0x10000);
        pit_irq.disable();
    }

    static void reset_pit()
    {
        if (not pit_irq.is_enabled()) return;

        const int next_count = pit_bios_count + pit_counter_max;
        if (std::abs(0x10000 - next_count) > 0x500) // +/- 1ms tolerance
        {
            // Adjust next cycle to avoid BIOS clock drift.
            const unsigned offset = 0x10000 - (next_count & 0xffff);
            write_pit(offset);
            wait_for_irq0 = true;
            pit_irq = []
            {
                irq0<false>();
                do_pit_reset();
            };
        }
        else do_pit_reset();
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
                wait_for_irq0 = tsc_calibrated;
            }

            if (freq_divisor < 2 or freq_divisor > 0x10000)
                throw std::out_of_range("Invalid PIT frequency divisor");

            pit_counter_new_max = freq_divisor;
            select_irq0_handler();
            pit_irq.enable();
            write_pit(freq_divisor);
        }
        if (wait_for_irq0)
        {
            dpmi::interrupt_unmask allow_irq { };
            do { } while (wait_for_irq0);
        }
    }

    void rtc::setup(bool enable, std::uint8_t freq_shift)
    {
        dpmi::interrupt_mask no_irq { };
        reset_rtc();
        if (not enable) return;

        if (freq_shift < 1 or freq_shift > 15)
            throw std::out_of_range { "Invalid RTC frequency shift" };

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

    void tsc::setup()
    {
        if (not dpmi::cpuid::feature_flags().time_stamp_counter) return;

        if (pit_irq.is_enabled())
            throw std::runtime_error { "Please call tsc::setup() before pit::setup()." };

        constexpr io::io_port<byte> pic0_mask { 0x21 };
        constexpr std::size_t N = 8;
        constexpr unsigned divisor = 0x1000;
        constexpr long double time = divisor / chrono::pit::max_frequency;

        std::array<std::uint64_t, N + 1> samples;
        auto* sample = samples.begin();

        {
            dpmi::interrupt_mask no_irq { };
            pit_irq = [&sample]
            {
                *sample++ = chrono::rdtsc();
                dpmi::irq_handler::acknowledge<0>();
            };
            pit_irq.enable();
            write_pit(divisor);

            // Mask all except IRQ 0.
            const auto irq_mask = pic0_mask.read();
            pic0_mask.write(0b11111110);

            {
                dpmi::interrupt_unmask allow_irq { };
                asm
                (R"(
                    .balign 0x10
                Loop:
                    .nops 14, 1
                    cmp [%0], %1
                    .nops 14, 1
                    jb Loop
                 )" :
                    : "r" (&sample),
                      "r" (samples.end())
                );
            }

            pic0_mask.write(irq_mask);

            write_pit(0x10000);
            pit_irq.disable();
        }

        std::array<std::uint32_t, N> counts;

        for (unsigned i = 0; i < N; ++i)
            counts[i] = samples[i + 1] - samples[i];

        std::uint32_t max_n = 0, n = 0;
        std::uint32_t most, current = 0;
        std::sort(counts.begin(), counts.end());

        // Find the most commonly occuring TSC count (statistical "mode")
        for (unsigned i = 0; i < N; ++i)
        {
            if (counts[i] == current) ++n;
            else
            {
                n = 1;
                current = counts[i];
            }

            if (n > max_n)
            {
                max_n = n;
                most = current;
            }
        }

        long double count = most;

        if (max_n < 3)
        {
            // TSC count mode not found.  This generally happens on emulators
            // only, or maybe if the DPMI host executes a non-deterministic
            // amount of code before calling the user IRQ handler.  Or,
            // several NMIs may have occured during sampling.
            // Discard the first 3 samples and calculate an average.
            std::uint64_t total = 0;
            for (unsigned i = 4; i < N + 1; ++i)
                total += samples[i] - samples[i - 1];
            count = static_cast<long double>(total) / (N - 3);
        }

        cpu_freq = count / time;
        float_ns_per_tsc_tick = 1e9 / cpu_freq;
        fixed_ns_per_tsc_tick = float_ns_per_tsc_tick;
        tsc_calibrated = true;
    }

    pit::time_point pit::now() noexcept
    {
        if (not pit_irq.is_enabled()) [[unlikely]]
            return time_point { std::chrono::steady_clock::now().time_since_epoch() };

        decltype(pit_ns) a, b;
        std::uint16_t counter;

        {
            dpmi::interrupt_mask no_irqs { };
            b.value = volatile_load(&pit_ns.value);
        }
        do
        {
            a = b;
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
        } while (a.value != b.value);

        a += ns_per_pit_count * (pit_counter_max - counter);
        return time_point { duration { static_cast<std::uint64_t>(a) + pit_ns_offset } };
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

    tsc::time_point tsc::now() noexcept
    {
        if (not tsc_calibrated or not pit_irq.is_enabled()) [[unlikely]]
            return time_point { pit::now().time_since_epoch() };

        decltype(pit_ns) pit;
        decltype(last_tsc) last;

        {
            dpmi::interrupt_mask no_irqs { };
            pit.value = volatile_load(&pit_ns.value);
            last = volatile_load(&last_tsc);
        }

        // This relies on unsigned integer roll-over, so only the lower 32 bits are needed here.
        const std::uint32_t tsc = rdtsc();
        const std::uint32_t count = tsc - last;
        const auto ns = pit + static_cast<decltype(pit_ns)>(fixed_ns_per_tsc_tick * count);
        return time_point { duration { static_cast<std::uint64_t>(ns) + pit_ns_offset } };
    }

    tsc::duration tsc::to_duration(tsc_count count)
    {
        return duration { static_cast<std::int64_t>(round(count * float_ns_per_tsc_tick)) };
    }

    long double tsc::cpu_frequency() noexcept { return cpu_freq; }

    struct reset_all
    {
        ~reset_all()
        {
            {
                dpmi::interrupt_mask no_irq { };
                reset_pit();
                reset_rtc();
            }
            if (wait_for_irq0)
            {
                dpmi::interrupt_unmask allow_irq { };
                do { } while (wait_for_irq0);
            }
        };
    } reset;
}
