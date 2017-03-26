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

#pragma once
#include <chrono>
#include <jw/dpmi/irq.h>
#include <jw/io/ioport.h>

namespace jw
{
    namespace chrono
    {
        inline std::uint64_t rdtsc() noexcept
        {
            std::uint64_t tsc;
            asm("rdtsc;" : "=A" (tsc));
            return tsc;
        }

        struct chrono
        {
            friend class system_clock;
            friend class steady_clock;
            friend class high_resolution_clock;

            static void setup_pit(bool enable);
            static void setup_rtc(bool enable);
        };

        struct system_clock             // RTC
        {
            using duration = std::chrono::microseconds;
            using rep = duration::rep;
            using period = duration::period;
            using time_point = std::chrono::time_point<system_clock>;

            static constexpr bool is_steady { false };
            static time_point now() noexcept;
            static std::time_t to_time_t (const time_point& t) noexcept;
            static time_point from_time_t(std::time_t t) noexcept;
        };

        struct steady_clock             // PIT
        {
            using duration = std::chrono::nanoseconds;
            using rep = duration::rep;
            using period = duration::period;
            using time_point = std::chrono::time_point<steady_clock>;

            static constexpr bool is_steady { true };
            static time_point now() noexcept;
        };

        struct high_resolution_clock    // RDTSC
        {
            using duration = std::chrono::nanoseconds;
            using rep = duration::rep;
            using period = duration::period;
            using time_point = std::chrono::time_point<high_resolution_clock>;

            static constexpr bool is_steady { false };
            static time_point now() noexcept;
        };
    }
}
