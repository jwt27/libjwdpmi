/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <deque>
#include <jw/common.h>
#include <jw/io/detail/scancode.h>
#include <jw/thread/task.h>

namespace jw
{
    namespace io
    {
        enum keyboard_leds : byte
        {
            scroll_lock_led = 0b001,
            num_lock_led = 0b010,
            caps_lock_led = 0b100
        };

        enum keyboard_response
        {
            ACK = 0xFA,
            RESEND = 0xFE,
            ERROR = 0xFC
        };

        class keyboard_interface
        {
        public:
            virtual std::deque<detail::scancode> get_scancodes() = 0;

            virtual scancode_set get_scancode_set() = 0;
            virtual void set_scancode_set(byte set) = 0;

            virtual void set_typematic(byte rate, byte delay) = 0;
            virtual void enable_typematic(bool enable) = 0;

            virtual void set_leds(keyboard_leds state) = 0;
            virtual void set_leds(bool num, bool caps, bool scroll)
            {
                set_leds(static_cast<keyboard_leds>(
                    (num ? keyboard_leds::num_lock_led : 0) |
                    (caps ? keyboard_leds::caps_lock_led : 0) |
                    (scroll ? keyboard_leds::scroll_lock_led : 0)));
            }

            virtual void set_keyboard_update_thread(thread::task<void()>) = 0;

            virtual ~keyboard_interface() { }
        };
    }
}
