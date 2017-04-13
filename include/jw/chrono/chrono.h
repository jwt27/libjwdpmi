/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <chrono>
#include <atomic>
#include <deque>
#include <jw/dpmi/irq.h>

namespace jw
{
    namespace chrono
    {
        inline std::uint64_t rdtsc() noexcept
        {
            std::uint64_t tsc;
            asm volatile ("rdtsc;": "=A" (tsc));
            return tsc;
        }

        enum class tsc_reference
        {
            none, rtc, pit
        };

        struct chrono
        {
            friend class rtc;
            friend class pit;
            friend class tsc;

            static constexpr long double max_pit_frequency { 1194375.0L / 1.001L };     // freq = max_pit_frequency / divider
            static constexpr std::uint32_t max_rtc_frequency { 0x8000 };                // freq = max_rtc_frequency >> (shift - 1)

            static void setup_pit(bool enable, std::uint32_t freq_divider = 0x10000);   // default: 18.2Hz
            static void setup_rtc(bool enable, std::uint8_t freq_shift = 10);           // default: 64Hz
            static void setup_tsc(std::size_t num_samples, tsc_reference ref = tsc_reference::none);

        private:
            static std::atomic<std::uint32_t> tsc_ticks_per_irq;
            static double ns_per_pit_tick;
            static double ns_per_rtc_tick;

            static volatile std::uint64_t pit_ticks;
            static volatile std::uint_fast16_t rtc_ticks;
            
            static dpmi::irq_handler pit_irq;
            static dpmi::irq_handler rtc_irq;

            INTERRUPT static void update_tsc();
            static void reset_pit();
            static void reset_rtc();
            static void reset_tsc();

            static tsc_reference preferred_tsc_ref;
            static tsc_reference current_tsc_ref()
            {
                if (preferred_tsc_ref == tsc_reference::pit && chrono::pit_irq.is_enabled()) return preferred_tsc_ref;
                else if (chrono::rtc_irq.is_enabled()) return tsc_reference::rtc;
                else if (chrono::pit_irq.is_enabled()) return tsc_reference::pit;
                else return tsc_reference::none;
            }

            static constexpr io::out_port<byte> rtc_index { 0x70 };
            static constexpr io::io_port<byte> rtc_data { 0x71 };

            struct reset_all { ~reset_all(); } static reset;
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
        };

        struct pit  // Programmable Interval Timer
        {
            using duration = std::chrono::nanoseconds;
            using rep = duration::rep;
            using period = duration::period;
            using time_point = std::chrono::time_point<pit>;

            static constexpr bool is_steady { false };
            static time_point now() noexcept
            {
                if (__builtin_expect(!chrono::pit_irq.is_enabled(), false))
                {
                    auto t = std::chrono::duration_cast<duration>(std::chrono::steady_clock::now().time_since_epoch());
                    return time_point { t };
                }
                return time_point { duration { static_cast<std::int64_t>(chrono::ns_per_pit_tick * chrono::pit_ticks) } };
            }
        };

        struct tsc  // Time Stamp Counter
        {
            using duration = std::chrono::nanoseconds;
            using rep = duration::rep;
            using period = duration::period;
            using time_point = std::chrono::time_point<tsc>;

            static constexpr bool is_steady { false };
            static time_point now() noexcept
            {
                if (__builtin_expect(!chrono::rtc_irq.is_enabled() && !chrono::pit_irq.is_enabled(), false))
                {
                    auto t = std::chrono::duration_cast<duration>(std::chrono::high_resolution_clock::now().time_since_epoch());
                    return time_point { t };
                }
                double ns = (chrono::current_tsc_ref() == tsc_reference::rtc) ? chrono::ns_per_rtc_tick : chrono::ns_per_pit_tick;
                ns *= rdtsc();
                ns /= chrono::tsc_ticks_per_irq;
                return time_point { duration { static_cast<std::int64_t>(ns) } };
            }
        };
    }
}
