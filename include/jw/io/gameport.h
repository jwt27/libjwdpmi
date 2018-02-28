/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/io/ioport.h>
#include <jw/chrono/chrono.h>
#include <jw/dpmi/irq.h>
#include <jw/thread/task.h>

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
                chrono::tsc_count x0_max { 0 };
                chrono::tsc_count y0_min { 0 };
                chrono::tsc_count y0_max { 0 };
                chrono::tsc_count x1_min { 0 };
                chrono::tsc_count x1_max { 0 };
                chrono::tsc_count y1_min { 0 };
                chrono::tsc_count y1_max { 0 };
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
        };

        using raw_t = value_t<chrono::tsc_count>;
        using normalized_t = value_t<float>;

        gameport(config c) : cfg(c), port(c.port) { }    // TODO
        ~gameport() { }

        auto get_raw()
        {
            return raw_t { };   // TODO
        }

        auto get()
        {
            return normalized_t { };     // TODO
        }

    private:
        struct [[gnu::packed]] raw_gameport
        {
            bool x0 : 1;
            bool y0 : 1;
            bool x1 : 1;
            bool y1 : 1;
            bool b0 : 1;
            bool b1 : 1;
            bool b2 : 1;
            bool b3 : 1;
        };
        static_assert(sizeof(raw_gameport) == 1);

        config cfg;
        io_port<raw_gameport> port;

        thread::task<void()> poll_task { [] 
        {
            while (true)
            {
                // TODO
            }
        } };
    };
}
