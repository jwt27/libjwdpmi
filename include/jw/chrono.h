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

    struct pit  // Programmable Interval Timer
    {
        using duration = std::chrono::duration<std::int64_t, std::nano>;
        using rep = duration::rep;
        using period = duration::period;
        using time_point = std::chrono::time_point<pit>;

        static constexpr long double max_frequency { 1194375.0L / 1.001L }; // freq = max_frequency / divisor
        static constexpr bool is_steady { false };

        static void setup(bool enable, std::uint32_t freq_divisor = 0x10000);   // default: 18.2Hz

        static time_point now() noexcept;

        static fixed<std::uint32_t, 6> irq_delta() noexcept;
    };

    struct tsc  // Time Stamp Counter
    {
        using duration = std::chrono::duration<std::int64_t, std::nano>;
        using rep = duration::rep;
        using period = duration::period;
        using time_point = std::chrono::time_point<tsc>;

        static constexpr bool is_steady { false };

        static void setup(timer_irq ref, std::size_t num_samples);  // num_samples must be a power of two

        static time_point now() noexcept;

        static duration to_duration(tsc_count count);

        static time_point to_time_point(tsc_count count)
        {
            return time_point { to_duration(count) };
        }
    };

    struct rtc  // Real-Time Clock
    {
        using duration = std::chrono::microseconds;
        using rep = duration::rep;
        using period = duration::period;
        using time_point = std::chrono::time_point<rtc>;

        static constexpr unsigned max_frequency { 0x8000 }; // freq = max_frequency >> (shift - 1)
        static constexpr bool is_steady { false };

        static void setup(bool enable, std::uint8_t freq_shift = 10);   // default: 64Hz

        static time_point now() noexcept;

        static std::time_t to_time_t(const time_point& t) noexcept
        {
            return std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
        }

        static time_point from_time_t(std::time_t t) noexcept
        {
            return time_point { std::chrono::duration_cast<duration>(std::chrono::seconds { t }) };
        }

        static double irq_delta() noexcept;
    };
}
