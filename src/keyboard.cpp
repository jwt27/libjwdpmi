/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#include <jw/io/keyboard.h>
#include <jw/io/detail/keyboard_streambuf.h>
#include <jw/dpmi/irq_mask.h>

namespace jw
{
    namespace io
    {
        void keyboard::update()
        {
            auto codes = interface->get_scancodes();
            if (codes.size() == 0) return;

            auto handle_key = [this](key_state_pair k) 
            {
                if (keys[k.first].is_down() && k.second.is_down()) k.second = key_state::repeat;

                keys[k.first] = k.second;

                keys[key::any_ctrl] = keys[key::ctrl_left] | keys[key::ctrl_right];
                keys[key::any_alt] = keys[key::alt_left] | keys[key::alt_right];
                keys[key::any_shift] = keys[key::shift_left] | keys[key::shift_right];
                keys[key::any_win] = keys[key::win_left] | keys[key::win_right];

                interface->set_leds(keys[key::num_lock_state].is_down(),
                                    keys[key::caps_lock_state].is_down(),
                                    keys[key::scroll_lock_state].is_down());

                key_changed(k);
            };

            for (auto c : codes)
            {
                auto k = c.decode();
                handle_key(k);

                static std::unordered_map<key, key> lock_key_table
                {
                    { key::num_lock, key::num_lock_state },
                    { key::caps_lock, key::caps_lock_state },
                    { key::scroll_lock, key::scroll_lock_state }
                };

                if (lock_key_table.count(k.first) && keys[k.first].is_up() && k.second.is_down())
                    handle_key({ lock_key_table[k.first], !keys[lock_key_table[k.first]] });
            }
        }

        void keyboard::redirect_cin(bool echo, std::ostream& echo_stream)
        {
            if (std::cin.rdbuf() != streambuf.get())
            {
                if (cin == nullptr) cin = std::cin.rdbuf();
                if (!streambuf) streambuf = std::make_unique<detail::keyboard_streambuf>(*this);
                std::cin.rdbuf(streambuf.get());
                auto_update(true);
            }
            auto* s = static_cast<detail::keyboard_streambuf*>(streambuf.get());
            s->echo = echo;
            s->echo_stream = &echo_stream;
            s->enable();
        }

        void keyboard::restore_cin()
        { 
            if (cin == nullptr || std::cin.rdbuf() != streambuf.get()) return;
            auto* s = static_cast<detail::keyboard_streambuf*>(streambuf.get());
            s->disable();
            std::cin.rdbuf(cin);
            cin = nullptr;
        }
    }
}
