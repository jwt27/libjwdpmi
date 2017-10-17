/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <unordered_map>
#include <memory>
#include <istream>

#include <jw/io/key.h>
#include <jw/io/detail/scancode.h>
#include <jw/io/keyboard_interface.h>
#include <jw/event.h>

namespace jw
{
    namespace io
    {
        struct keyboard final
        {
            event<void(key_state_pair)> key_changed;

            const key_state& get(key k) const { return keys[k]; }
            const key_state& operator[](key k) const { return keys[k]; }

            void redirect_cin(bool echo = true, std::ostream& echo_stream = std::cout);
            void restore_cin();
            void update();

            void auto_update(bool enable)
            {
                if (enable) interface->set_keyboard_update_thread({ [this]() { update(); } });
                else interface->set_keyboard_update_thread({ });
            }

            keyboard(std::shared_ptr<keyboard_interface> intf) : interface(intf) { }
            ~keyboard() { restore_cin(); }

        private:
            std::shared_ptr<keyboard_interface> interface;
            mutable std::unordered_map<key, key_state> keys { };
            std::unique_ptr<std::streambuf> streambuf;
            static std::streambuf* cin;
            static bool cin_redirected;
        };
    }
}
