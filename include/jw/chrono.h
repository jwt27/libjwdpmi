/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once

#include <atomic>
#include <deque>
#include <chrono>
#include <jw/dpmi/irq_handler.h>
#include <jw/math.h>
#include <jw/fixed.h>

namespace jw::chrono
{
    using tsc_count = std::uint64_t;

    inline tsc_count rdtsc()
    {
        tsc_count tsc;
        asm volatile ("rdtsc" : "=A" (tsc));
        return tsc;
    }

    inline tsc_count rdtscp()
    {
        tsc_count tsc;
        asm volatile ("cpuid; rdtsc" : "=A" (tsc) : "a" (0) : "ebx", "ecx");
        return tsc;
    }

    enum class timer_irq
    {
        none = -1,
        pit = 0,
        rtc = 8
    };

    // Programmable Interval Timer
    struct pit
    {
        using duration = std::chrono::nanoseconds;
        using rep = duration::rep;
        using period = duration::period;
        using time_point = std::chrono::time_point<pit>;

        static constexpr long double max_frequency { 1194375.0L / 1.001L };
        static constexpr bool is_steady { false };

        // Enable or disable the PIT interrupt (IRQ 0) and reprogram it to
        // trigger at a specific frequency.  The divisor can be calculated as:
        //      freq_divisor = round(max_frequency / desired_frequency)
        // Valid values are in the range [1 .. 0x10000].  The default value
        // (0x10000) corresponds to ~18.2Hz.  The interrupt frequency may be
        // changed on the fly, without invalidating previous time points.
        static void setup(bool enable, std::uint32_t freq_divisor = 0x10000);

        // Returns the current UNIX time.  This has a fixed resolution of
        // 838.1ns, regardless of interrupt frequency.  If the PIT IRQ is not
        // enabled, returns std::chrono::steady_clock::now(), which has about
        // ~55ms resolution.
        static time_point now() noexcept;

        // Returns the time interval between interrupts in nanoseconds.
        static fixed<std::uint32_t, 6> irq_delta() noexcept;
    };

    // Time Stamp Counter
    struct tsc
    {
        using duration = std::chrono::nanoseconds;
        using rep = duration::rep;
        using period = duration::period;
        using time_point = std::chrono::time_point<tsc>;

        static constexpr bool is_steady { false };

        // Calibrate rdtsc using the PIT.  This must be done *before* calling
        // pit::setup().  A calibration cycle takes ~28ms, during which
        // interrupts will be disabled.
        static void setup();

        // Returns the current UNIX time.  Resolution is dependent on the CPU
        // frequency, eg. 2ns on a 500MHz CPU.  If the CPU does not support
        // rdtsc, this returns pit::now().
        static time_point now() noexcept;

        // Convert a tsc_count to a duration using the calibration values
        // from tsc::setup().  This is most accurate for short intervals.
        // The return value is undefined if the CPU does not support rdtsc.
        static duration to_duration(tsc_count count);

        // Returns the CPU frequency as measured by tsc::setup().
        static long double cpu_frequency() noexcept;
    };

    // Real-Time Clock
    struct rtc
    {
        using duration = std::chrono::microseconds;
        using rep = duration::rep;
        using period = duration::period;
        using time_point = std::chrono::time_point<rtc>;

        static constexpr unsigned max_frequency { 0x8000 };
        static constexpr bool is_steady { false };

        // Enable the RTC interrupt (IRQ 8) and reprogram it to trigger at a
        // specific frequency.  This frequency may be calculated with:
        //      f = max_frequency >> (freq_shift - 1)
        // Valid shift values are in the range [1 .. 15].  The default value
        // corresponds to 64Hz.
        static void setup(bool enable, std::uint8_t freq_shift = 10);

        // Returns the current UNIX time.  This always reads the RTC directly,
        // so this call is very slow.
        static time_point now() noexcept;

        static std::time_t to_time_t(const time_point& t) noexcept
        {
            return std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
        }

        static time_point from_time_t(std::time_t t) noexcept
        {
            return time_point { std::chrono::duration_cast<duration>(std::chrono::seconds { t }) };
        }

        // Retuns the time interval between interrupts in nanoseconds.
        static double irq_delta() noexcept;
    };
}
