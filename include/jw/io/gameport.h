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
#include <jw/vector.h>
#include <../jwdpmi_config.h>
#include <limits>
#include <optional>
#include <bitset>
#include <experimental/deque>

// TODO: centering / deadzone
// TODO: auto-calibrate?

namespace jw::io
{
    struct gameport
    {
        using clock = jw::config::gameport_clock;

        enum class poll_strategy
        {
            busy_loop,
            pit_irq,
            rtc_irq,
            thread
        };

        template<typename T>
        struct value_t
        {
            union
            {
                std::array<T, 4> a;
                struct { T x, y, z, w; };
            };

            constexpr const T& operator[](std::ptrdiff_t i) const { return (a[i]); }
            constexpr T& operator[](std::ptrdiff_t i) { return (a[i]); }

            constexpr value_t(T vx0, T vy0, T vx1, T vy1) noexcept : x(vx0), y(vy0), z(vx1), w(vy1) { }
            constexpr value_t(T v) noexcept : value_t(v, v, v, v) { }
            constexpr value_t() noexcept = default;
        };

        using raw_t = value_t<typename clock::duration>;
        using normalized_t = vector4f;

        struct config
        {
            port_num port { 0x201 };
            poll_strategy strategy { poll_strategy::busy_loop };
            chrono::tsc::duration smoothing_window { std::chrono::milliseconds { 50 } };

            value_t<bool> enable { true };

            struct
            {
                raw_t min { std::chrono::milliseconds { 0 } }, max { std::chrono::milliseconds { 25 } };
            } calibration;

            struct
            {
                normalized_t max { +1, +1, +1, +1 };
                normalized_t min { -1, -1, -1, -1 };
            } output_range;
        } cfg;

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
            raw_t value { typename clock::duration { 0 } };
            auto& c = cfg.calibration;

            for (auto&& s : samples)
            {
                for (auto i = 0; i < 4; ++i)
                    value.a[i] += s.first.a[i] - c.min.a[0];
            }
            for (auto&& s : value.a) s /= samples.size();

            return value;
        }

        auto get()
        {
            auto& c = cfg.calibration;
            auto& o = cfg.output_range;

            auto raw = get_raw();
            normalized_t value;

            for (auto i = 0; i < 4; ++i)
            {
                value[i] = raw[i].count();
                value[i] /= c.max[i].count() - c.min[i].count();
                value[i] = o.min[i] + value[i] * (o.max[i] - o.min[i]);
            }

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

        io_port<raw_gameport> port;
        raw_t sample;
        std::bitset<4> timing { false };
        typename clock::time_point timing_start;
        button_t button_state;
        std::optional<dpmi::data_lock> lock;
        std::unique_ptr<std::experimental::pmr::memory_resource> memory_resource;
        std::experimental::pmr::deque<std::pair<button_t, typename clock::time_point>> button_events { get_memory_resource() };
        std::experimental::pmr::deque<std::pair<raw_t, typename clock::time_point>> samples { get_memory_resource() };

        bool using_irq() const { return cfg.strategy == poll_strategy::pit_irq or cfg.strategy == poll_strategy::rtc_irq; }
        std::experimental::pmr::memory_resource* get_memory_resource() const noexcept { if (using_irq()) return memory_resource.get(); else return std::experimental::pmr::get_default_resource(); }

        void update_buttons(raw_gameport p, typename clock::time_point now)
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
            decltype(clock::now()) now;
            if (timing.none())
            {
                for (auto i = 0; i < 4; ++i)
                    timing[i] = cfg.enable.a[i];
                port.write({ });
                timing_start = clock::now();
            }
            do
            {
                auto p = port.read();
                now = clock::now();
                auto& c = cfg.calibration;
                auto i = now - timing_start;
                if (timing[0] and (not p.x0 or i > c.max[0])) { timing[0] = false; sample[0] = std::clamp(i, c.min[0], c.max[0]); }
                if (timing[1] and (not p.y0 or i > c.max[1])) { timing[1] = false; sample[1] = std::clamp(i, c.min[1], c.max[1]); }
                if (timing[2] and (not p.x1 or i > c.max[2])) { timing[2] = false; sample[2] = std::clamp(i, c.min[2], c.max[2]); }
                if (timing[3] and (not p.y1 or i > c.max[3])) { timing[3] = false; sample[3] = std::clamp(i, c.min[3], c.max[3]); }
                update_buttons(p, now);
            } while (cfg.strategy == poll_strategy::busy_loop and timing.any());
            if (timing.none()) samples.emplace_back(sample, now);

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
