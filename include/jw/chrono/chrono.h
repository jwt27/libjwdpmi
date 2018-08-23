/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once

#include <atomic>
#include <deque>
#include <jw/dpmi/irq.h>
#include <jw/math.h>

// included by <chrono>
#include <ratio>
#include <type_traits>
#include <limits>
#include <ctime>
#include <bits/parse_numbers.h>

#define inline // eww
#include_next <chrono>
#undef inline

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
            static constexpr std::uint32_t max_rtc_frequency { 0x8000 };                // freq = max_rtc_frequency >> (shift - 1)

            static void setup_pit(bool enable, std::uint32_t freq_divisor = 0x10000);   // default: 18.2Hz
            static void setup_rtc(bool enable, std::uint8_t freq_shift = 10);           // default: 64Hz
            static void setup_tsc(std::size_t num_samples, tsc_reference ref = tsc_reference::none);

        private:
            static inline std::atomic<std::uint32_t> tsc_ticks_per_irq { 0 };
            static constexpr double ns_per_pit_count { 1e9 / max_pit_frequency };
            static inline double ns_per_pit_tick;
            static inline double ns_per_rtc_tick;

            static inline std::uint32_t pit_counter_max;
            static inline volatile std::uint64_t pit_ticks;
            static inline volatile std::uint_fast16_t rtc_ticks;
            
            static dpmi::irq_handler pit_irq;
            static dpmi::irq_handler rtc_irq;

            INTERRUPT static void update_tsc();
            static void reset_pit();
            static void reset_rtc();
            static void reset_tsc();

            static inline bool have_rdtsc { false };
            static inline tsc_reference preferred_tsc_ref { tsc_reference::pit };
            static tsc_reference current_tsc_ref()
            {
                if (preferred_tsc_ref == tsc_reference::pit && setup::pit_irq.is_enabled()) return preferred_tsc_ref;
                else if (setup::rtc_irq.is_enabled()) return tsc_reference::rtc;
                else if (setup::pit_irq.is_enabled()) return tsc_reference::pit;
                else return tsc_reference::none;
            }

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
                if (__builtin_expect(not setup::pit_irq.is_enabled(), false))
                {
                    auto t = std::chrono::duration_cast<duration>(std::chrono::_V2::steady_clock::now().time_since_epoch());
                    return time_point { t };
                }
                dpmi::interrupt_mask no_irqs { };
                setup::pit_cmd.write(0x00);        // latch counter 0
                split_uint16_t counter { setup::pit0_data.read(), setup::pit0_data.read() };
                double ns { setup::ns_per_pit_count * (setup::pit_counter_max - counter) + setup::ns_per_pit_tick * setup::pit_ticks };
                return time_point { duration { static_cast<std::int64_t>(jw::round(ns)) } };
            }
        };

        struct tsc  // Time Stamp Counter
        {
            using duration = std::chrono::nanoseconds;
            using rep = duration::rep;
            using period = duration::period;
            using time_point = std::chrono::time_point<tsc>;

            static constexpr bool is_steady { false };

            static duration to_duration(tsc_count count)
            {
                double ns = (setup::current_tsc_ref() == tsc_reference::rtc) ? setup::ns_per_rtc_tick : setup::ns_per_pit_tick;
                ns *= count;
                ns /= setup::tsc_ticks_per_irq;
                return duration { static_cast<std::int64_t>(jw::round(ns)) };
            }

            static time_point to_time_point(tsc_count count)
            {
                return time_point { to_duration(count) };
            }

            static time_point now() noexcept
            {
                if (__builtin_expect(not setup::rtc_irq.is_enabled() and not setup::pit_irq.is_enabled(), false))
                {
                    auto t = std::chrono::duration_cast<duration>(std::chrono::_V2::high_resolution_clock::now().time_since_epoch());
                    return time_point { t };
                }
                if (__builtin_expect(not setup::have_rdtsc, false))
                {
                    return time_point { std::chrono::duration_cast<duration>(pit::now().time_since_epoch()) };
                }
                return to_time_point(rdtsc());
            }
        };
    }
}

namespace std
{
    namespace chrono
    {
        inline namespace jw
        {
            using system_clock = ::jw::chrono::rtc;
            using steady_clock = ::jw::chrono::pit;
            using high_resolution_clock = ::jw::chrono::tsc;
        }
    }

    inline namespace literals
    {
        using namespace chrono_literals;
    }
    using namespace literals;
}
