/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/io/ioport.h>
#include <jw/chrono/chrono.h>
#include <jw/dpmi/irq.h>
#include <jw/thread/task.h>
#include <jw/dpmi/lock.h>
#include <jw/dpmi/alloc.h>
#include <jw/event.h>
#include <limits>
#include <optional>
#include <experimental/deque>

// TODO: centering
// TODO: auto-calibrate?

namespace jw::io
{
    template <typename Clock = chrono::tsc>
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
            chrono::tsc::duration smoothing_window { std::chrono::milliseconds { 50 } };

            struct
            {
                bool x0 { true }, y0 { true };
                bool x1 { true }, y1 { true };
            } enable;

            struct
            {
                using T = typename Clock::duration;
                T x0_min { 0 };
                T y0_min { 0 };
                T x1_min { 0 };
                T y1_min { 0 };
                T x0_max { std::chrono::milliseconds { 25 } };
                T y0_max { std::chrono::milliseconds { 25 } };
                T x1_max { std::chrono::milliseconds { 25 } };
                T y1_max { std::chrono::milliseconds { 25 } };
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

        using raw_t = value_t<typename Clock::duration>;
        using normalized_t = vector_t<float>;

        struct button_t
        {
            bool a0, b0, a1, b1;
            constexpr bool operator==(const button_t& o) const noexcept { return a0 == o.a0 and b0 == o.b0 and a1 == o.a1 and b1 == o.b1; }
            constexpr bool operator!=(const button_t& o) const noexcept { return not (o == *this); }
        };

        gameport(config c, std::size_t alloc_size = 1_KB) : cfg(c), port(c.port),
            memory_resource(using_irq()? std::make_unique<dpmi::locked_pool_memory_resource>(alloc_size) : nullptr)
        {
            switch (cfg.strategy)
            {
            case poll_strategy::pit_irq:
                poll_irq.set_irq(0);
                break;
            case poll_strategy::rtc_irq:
                poll_irq.set_irq(8);
                break;
            default:
                break;
            }
            if (using_irq())
            {
                lock = std::make_optional<dpmi::data_lock>(this);
                poll_irq.enable();
            }
            poll_task->start();
        }

        ~gameport()
        {
            poll_irq.disable();
            if (poll_task->is_running()) poll_task->abort();
        }

        auto get_raw()
        {
            poll();
            raw_t value { typename Clock::duration { 0 } };
            auto& c = cfg.calibration;

            for (auto&& s : samples)
            {
                value.x0 += s.first.x0 - c.x0_min;     // TODO: generic vector4 class
                value.y0 += s.first.y0 - c.y0_min;
                value.x1 += s.first.x1 - c.x1_min;
                value.y1 += s.first.y1 - c.y1_min;
            }
            value.x0 /= samples.size();
            value.y0 /= samples.size();
            value.x1 /= samples.size();
            value.y1 /= samples.size();

            return value;
        }

        auto get()
        {
            auto& c = cfg.calibration;
            auto& o = cfg.output_range;

            auto raw = get_raw();
            normalized_t value;

            value.x0 = raw.x0.count();
            value.y0 = raw.y0.count();
            value.x1 = raw.x1.count();
            value.y1 = raw.y1.count();

            value.x0 /= c.x0_max.count() - c.x0_min.count();
            value.y0 /= c.y0_max.count() - c.y0_min.count();
            value.x1 /= c.x1_max.count() - c.x1_min.count();
            value.y1 /= c.y1_max.count() - c.y1_min.count();

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

        event<void(button_t, chrono::tsc::time_point)> button_changed;

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
        raw_t sample;
        value_t<bool> timing { false };
        typename Clock::time_point timing_start;
        button_t button_state;
        std::optional<dpmi::data_lock> lock;
        std::unique_ptr<std::experimental::pmr::memory_resource> memory_resource;
        std::experimental::pmr::deque<std::pair<button_t, typename Clock::time_point>> button_events { get_memory_resource() };
        std::experimental::pmr::deque<std::pair<raw_t, typename Clock::time_point>> samples { get_memory_resource() };

        bool using_irq() const { return cfg.strategy == poll_strategy::pit_irq or cfg.strategy == poll_strategy::rtc_irq; }
        std::experimental::pmr::memory_resource* get_memory_resource() const noexcept { if (using_irq()) return memory_resource.get(); else return std::experimental::pmr::get_default_resource(); }

        void update_buttons(raw_gameport p, typename Clock::time_point now)
        {
            button_t x;
            x.a0 = not p.a0;
            x.b0 = not p.b0;
            x.a1 = not p.a1;
            x.b1 = not p.b1;
            if (x != button_state) button_events.emplace_back(x, now);
            button_state = x;
        }

        void poll()
        {
            auto timing_done = [this] { return not (timing.x0 or timing.y0 or timing.x1 or timing.y1); };
            decltype(Clock::now()) now;
            if (timing_done())
            {
                timing.x0 = cfg.enable.x0;
                timing.y0 = cfg.enable.y0;
                timing.x1 = cfg.enable.x1;
                timing.y1 = cfg.enable.y1;
                port.write({ });
                timing_start = Clock::now();
            }
            do
            {
                auto p = port.read();
                now = Clock::now();
                auto& c = cfg.calibration;
                auto i = now - timing_start;
                if (timing.x0 and (not p.x0 or i > c.x0_max)) { timing.x0 = false; sample.x0 = std::clamp(i, c.x0_min, c.x0_max); }
                if (timing.y0 and (not p.y0 or i > c.y0_max)) { timing.y0 = false; sample.y0 = std::clamp(i, c.y0_min, c.y0_max); }
                if (timing.x1 and (not p.x1 or i > c.x1_max)) { timing.x1 = false; sample.x1 = std::clamp(i, c.x1_min, c.x1_max); }
                if (timing.y1 and (not p.y1 or i > c.y1_max)) { timing.y1 = false; sample.y1 = std::clamp(i, c.y1_min, c.y1_max); }
                update_buttons(p, now);
            } while (cfg.strategy == poll_strategy::busy_loop and not timing_done());
            if (timing_done()) samples.emplace_back(sample, now);

            for (auto i = samples.begin(); i != samples.end();)
            {
                if (samples.size() > 1 and i->second < now - cfg.smoothing_window)
                    i = samples.erase(i);
                else break;
            }
            while (samples.size() == 0) poll();
        }

        thread::task<void()> poll_task { [this]
        {
            while (true)
            {
                if (cfg.strategy != poll_strategy::busy_loop) poll();
                for (auto i = button_events.begin(); i != button_events.end(); i = button_events.erase(i))
                    button_changed(i->first, i->second);
                thread::yield();
            }
        } };

        dpmi::irq_handler poll_irq { [this]
        {
            poll();
        }, dpmi::always_call };
    };
}
