/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/io/ioport.h>
#include <jw/chrono/chrono.h>
#include <jw/dpmi/irq.h>
#include <jw/thread/task.h>
#include <limits>

namespace jw::io
{
    struct gameport
    {
        enum class poll_strategy
        {
            busy_loop,
            pit_irq,
            rtc_irq,
            thread
        };

        struct config
        {
            port_num port { 0x201 };
            poll_strategy strategy { poll_strategy::busy_loop };

            struct
            {
                bool x0 { true }, y0 { true };
                bool x1 { true }, y1 { true };
            } enable;

            struct
            {
                chrono::tsc_count x0_min { std::numeric_limits<chrono::tsc_count>::max() };
                chrono::tsc_count x0_max { std::numeric_limits<chrono::tsc_count>::max() };
                chrono::tsc_count y0_min { std::numeric_limits<chrono::tsc_count>::max() };
                chrono::tsc_count y0_max { std::numeric_limits<chrono::tsc_count>::max() };
                chrono::tsc_count x1_min { std::numeric_limits<chrono::tsc_count>::max() };
                chrono::tsc_count x1_max { std::numeric_limits<chrono::tsc_count>::max() };
                chrono::tsc_count y1_min { std::numeric_limits<chrono::tsc_count>::max() };
                chrono::tsc_count y1_max { std::numeric_limits<chrono::tsc_count>::max() };
            } calibration;

            struct
            {
                float x0_min { -1 };
                float x0_max { +1 };
                float y0_min { -1 };
                float y0_max { +1 };
                float x1_min { -1 };
                float x1_max { +1 };
                float y1_min { -1 };
                float y1_max { +1 };
            } output_range;
        };

        template<typename T>
        struct value_t
        {
            T x0, y0, x1, y1;
            constexpr value_t(T vx0, T vy0, T vx1, T vy1) noexcept : x0(vx0), y0(vy0), x1(vx1), y1(vy1) { }
            constexpr value_t(T v) noexcept : value_t(v, v, v, v) { }
            constexpr value_t() noexcept = default;
        };

        using raw_t = value_t<chrono::tsc_count>;
        using normalized_t = value_t<float>;

        struct button_t
        {
            bool a0, b0, a1, b1;
        };

        gameport(config c) : cfg(c), port(c.port) { }    // TODO
        ~gameport() { }

        auto get_raw()
        {
            poll();
            return last;
        }

        auto get()
        {
            poll();
            return normalized_t { };     // TODO
        }

        auto buttons() const
        {
            return button_state;
        }

    private:
        struct [[gnu::packed]] raw_gameport
        {
            bool x0 : 1;
            bool y0 : 1;
            bool x1 : 1;
            bool y1 : 1;
            bool a0 : 1;
            bool b0 : 1;
            bool a1 : 1;
            bool b1 : 1;
        };
        static_assert(sizeof(raw_gameport) == 1);

        const config cfg;
        io_port<raw_gameport> port;
        raw_t last;
        value_t<bool> timing { false };
        chrono::tsc_count timing_start;
        button_t button_state;

        void update_buttons(raw_gameport p) // TODO: events
        {
            button_state.a0 = not p.a0;
            button_state.b0 = not p.b0;
            button_state.a1 = not p.a1;
            button_state.b1 = not p.b1;
        }

        void poll()
        {
            if (not timing.x0 and not timing.y0 and not timing.x1 and not timing.y1)
            {
                port.write({ });
                timing_start = chrono::rdtsc();
            }
            do
            {
                auto p = port.read();
                auto now = chrono::rdtsc();
                if (timing.x0 and not p.x0) { timing.x0 = false; if (now <= cfg.calibration.x0_max) last.x0 = now - timing_start; }
                if (timing.y0 and not p.y0) { timing.y0 = false; if (now <= cfg.calibration.y0_max) last.y0 = now - timing_start; }
                if (timing.x1 and not p.x1) { timing.x1 = false; if (now <= cfg.calibration.x1_max) last.x1 = now - timing_start; }
                if (timing.y1 and not p.y1) { timing.y1 = false; if (now <= cfg.calibration.y1_max) last.y1 = now - timing_start; }
                update_buttons(p);
            } while (cfg.strategy == poll_strategy::busy_loop and (timing.x0 or timing.y0 or timing.x1 or timing.y1));
        }

        thread::task<void()> poll_task { [this]
        {
            while (true)
            {
                poll();
                thread::yield();
            }
        } };

        dpmi::irq_handler poll_irq { [this]
        {
            poll();
        }, dpmi::always_call };
    };
}
