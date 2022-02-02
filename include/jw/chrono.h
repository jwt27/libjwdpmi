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

namespace jw
{
    namespace chrono
    {
        using tsc_count = std::uint64_t;

        inline tsc_count rdtsc()
        {
            tsc_count tsc;
            asm volatile ("rdtsc;": "=A" (tsc));
            return tsc;
        }

        inline tsc_count rdtscp()
        {
            tsc_count tsc;
            asm volatile ("cpuid;rdtsc;": "=A" (tsc): "a" (0) : "ebx", "ecx");
            return tsc;
        }

        enum class tsc_reference
        {
            none, rtc, pit
        };

        struct setup
        {
            friend class rtc;
            friend class pit;
            friend class tsc;

            static constexpr long double max_pit_frequency { 1194375.0L / 1.001L };     // freq = max_pit_frequency / divisor
            static constexpr unsigned max_rtc_frequency { 0x8000 };                     // freq = max_rtc_frequency >> (shift - 1)

            static void setup_pit(bool enable, std::uint32_t freq_divisor = 0x10000);   // default: 18.2Hz
            static void setup_rtc(bool enable, std::uint8_t freq_shift = 10);           // default: 64Hz
            static void setup_tsc(std::size_t num_samples, tsc_reference ref = tsc_reference::none);    // num_samples must be a power of two

        private:
            static inline std::atomic<std::uint32_t> tsc_ticks_per_irq { 0 };
            static constexpr fixed<std::uint32_t, 6> ns_per_pit_count { 1e9 / max_pit_frequency };
            static inline fixed<std::uint32_t, 6> ns_per_pit_tick;
            static inline double ns_per_rtc_tick;

            static inline std::uint32_t pit_counter_max;
            static inline volatile std::uint64_t pit_ticks;
            static inline volatile std::uint_fast16_t rtc_ticks;

            static dpmi::irq_handler pit_irq;
            static dpmi::irq_handler rtc_irq;

            static void update_tsc();
            static void reset_pit();
            static void reset_rtc();
            static void reset_tsc();

            static inline tsc_reference tsc_ref { tsc_reference::none };

            static inline constexpr io::out_port<byte> rtc_index { 0x70 };
            static inline constexpr io::io_port<byte> rtc_data { 0x71 };
            static inline constexpr io::out_port<byte> pit_cmd { 0x43 };
            static inline constexpr io::io_port<byte> pit0_data { 0x40 };

            struct reset_all { ~reset_all(); } static inline reset;
        };

        struct rtc  // Real-Time Clock
        {
            using duration = std::chrono::microseconds;
            using rep = duration::rep;
            using period = duration::period;
            using time_point = std::chrono::time_point<rtc>;

            static constexpr bool is_steady { false };
            static time_point now() noexcept;

            static std::time_t to_time_t(const time_point& t) noexcept
            {
                return std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
            }

            static time_point from_time_t(std::time_t t) noexcept
            {
                return time_point { std::chrono::duration_cast<duration>(std::chrono::seconds { t }) };
            }

            static auto irq_delta() { return setup::ns_per_rtc_tick; }
        };

        struct pit  // Programmable Interval Timer
        {
            using duration = std::chrono::duration<std::int64_t, std::nano>;
            using rep = duration::rep;
            using period = duration::period;
            using time_point = std::chrono::time_point<pit>;

            static constexpr bool is_steady { false };
            static time_point now() noexcept;

            static auto irq_delta() { return setup::ns_per_pit_tick; }
        };

        struct tsc  // Time Stamp Counter
        {
            using duration = std::chrono::duration<std::int64_t, std::nano>;
            using rep = duration::rep;
            using period = duration::period;
            using time_point = std::chrono::time_point<tsc>;

            static constexpr bool is_steady { false };
            static time_point now() noexcept;

            static duration to_duration(tsc_count count)
            {
                double ns;
                switch (setup::tsc_ref)
                {
                case tsc_reference::pit: ns = setup::ns_per_pit_tick; break;
                case tsc_reference::rtc: ns = setup::ns_per_rtc_tick; break;
                default: [[unlikely]] return duration::min();
                }
                ns *= count;
                ns /= setup::tsc_ticks_per_irq;
                return duration { static_cast<std::uint64_t>(round(ns)) };
            }

            static time_point to_time_point(tsc_count count)
            {
                return time_point { to_duration(count) };
            }
        };
    }
}
