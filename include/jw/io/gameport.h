/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/io/ioport.h>
#include <jw/chrono/chrono.h>
#include <jw/dpmi/irq.h>
#include <jw/thread/task.h>
#include <limits>

// TODO: smoothing
// TODO: centering
// TODO: auto-calibrate?

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
                chrono::tsc_count x0_min { 0 };
                chrono::tsc_count y0_min { 0 };
                chrono::tsc_count x1_min { 0 };
                chrono::tsc_count y1_min { 0 };
                chrono::tsc_count x0_max { std::numeric_limits<chrono::tsc_count>::max() };
                chrono::tsc_count y0_max { std::numeric_limits<chrono::tsc_count>::max() };
                chrono::tsc_count x1_max { std::numeric_limits<chrono::tsc_count>::max() };
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

        template<typename T>
        struct vector_t
        {
            using V[[gnu::vector_size(2 * sizeof(T))]] = T;
            union
            {
                V v;
                struct { T x0, y0, x1, y1; };
            };

            constexpr vector_t(T vx0, T vy0, T vx1, T vy1) noexcept : x0(vx0), y0(vy0), x1(vx1), y1(vy1) { }
            constexpr vector_t(T v) noexcept : vector_t(v, v, v, v) { }
            constexpr vector_t() noexcept = default;
        };

        using raw_t = value_t<chrono::tsc_count>;
        using normalized_t = vector_t<float>;

        struct button_t
        {
            bool a0, b0, a1, b1;
        };

        gameport(config c) : cfg(c), port(c.port)
        {
            switch (cfg.strategy)
            {
            case poll_strategy::thread:
                poll_task->start();
                break;
            case poll_strategy::pit_irq:
                poll_irq.set_irq(0);
                poll_irq.enable();
                break;
            case poll_strategy::rtc_irq:
                poll_irq.set_irq(8);
                poll_irq.enable();
                break;
            case poll_strategy::busy_loop:
                break;
            }
        }

        ~gameport()
        {
            poll_irq.disable();
            if (poll_task->is_running()) poll_task->abort();
        }

        auto get_raw()
        {
            poll();
            return last;
        }

        auto get()
        {
            auto raw = get_raw();
            normalized_t value;

            auto& c = cfg.calibration;
            auto& o = cfg.output_range;

            value.x0 = static_cast<float>(raw.x0 - c.x0_min) / (c.x0_max - c.x0_min);
            value.y0 = static_cast<float>(raw.y0 - c.y0_min) / (c.y0_max - c.y0_min);
            value.x1 = static_cast<float>(raw.x1 - c.x1_min) / (c.x1_max - c.x1_min);
            value.y1 = static_cast<float>(raw.y1 - c.y1_min) / (c.y1_max - c.y1_min);

            value.x0 = o.x0_min + value.x0 * (o.x0_max - o.x0_min);
            value.y0 = o.y0_min + value.y0 * (o.y0_max - o.y0_min);
            value.x1 = o.x1_min + value.x1 * (o.x1_max - o.x1_min);
            value.y1 = o.y1_min + value.y1 * (o.y1_max - o.y1_min);

            return value;
        }

        auto buttons()
        {
            if (cfg.strategy != poll_strategy::busy_loop) poll();
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
                timing.x0 = cfg.enable.x0;
                timing.y0 = cfg.enable.y0;
                timing.x1 = cfg.enable.x1;
                timing.y1 = cfg.enable.y1;
                port.write({ });
                timing_start = chrono::rdtsc();
            }
            do
            {
                auto p = port.read();
                auto now = chrono::rdtsc();
                auto& c = cfg.calibration;
                auto i = now - timing_start;
                if (timing.x0 and (not p.x0 or i > c.x0_max)) { timing.x0 = false; last.x0 = std::clamp(i, c.x0_min, c.x0_max); }
                if (timing.y0 and (not p.y0 or i > c.y0_max)) { timing.y0 = false; last.y0 = std::clamp(i, c.y0_min, c.y0_max); }
                if (timing.x1 and (not p.x1 or i > c.x1_max)) { timing.x1 = false; last.x1 = std::clamp(i, c.x1_min, c.x1_max); }
                if (timing.y1 and (not p.y1 or i > c.y1_max)) { timing.y1 = false; last.y1 = std::clamp(i, c.y1_min, c.y1_max); }
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
