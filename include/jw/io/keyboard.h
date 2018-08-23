/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
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
            chain_event<bool(key_state_pair)> key_changed;

            const key_state& get(key k) const { return keys[k]; }
            const key_state& operator[](key k) const { return keys[k]; }

            void redirect_cin(bool echo = true, std::ostream& echo_stream = std::cout);
            void restore_cin();
            void update() { do_update(false); }

            void auto_update(bool enable)
            {
                if (enable) interface->set_keyboard_update_thread({ [this]() { do_update(true); } });
                else interface->set_keyboard_update_thread({ });
            }

            keyboard(std::shared_ptr<keyboard_interface> intf) : interface(intf) { }
            ~keyboard() { restore_cin(); }

        private:
            std::shared_ptr<keyboard_interface> interface;
            mutable std::unordered_map<key, key_state> keys { };
            std::unique_ptr<std::streambuf> streambuf;
            static inline std::streambuf* cin { nullptr };
            static inline bool cin_redirected { false };
            void do_update(bool);
        };
    }
}
