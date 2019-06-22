/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#include <exception>
#include <jw/io/keyboard.h>
#include <jw/io/detail/keyboard_streambuf.h>
#include <jw/dpmi/irq_mask.h>

namespace jw
{
    namespace io
    {
        void keyboard::do_update(bool async)
        {
            try
            {
                auto handle_key = [this](key_state_pair k)
                {
                    if (keys[k.first].is_down() && k.second.is_down()) k.second = key_state::repeat;

                    keys[k.first] = k.second;

                    keys[key::any_ctrl] = keys[key::ctrl_left] | keys[key::ctrl_right];
                    keys[key::any_alt] = keys[key::alt_left] | keys[key::alt_right];
                    keys[key::any_shift] = keys[key::shift_left] | keys[key::shift_right];
                    keys[key::any_win] = keys[key::win_left] | keys[key::win_right];
                    keys[key::any_enter] = keys[key::enter] | keys[key::num_enter];

                    key_changed(k);
                };

                while (auto c = ps2->get_scancode())
                {
                    auto k = c->decode();
                    handle_key(k);

                    auto set_lock_state = [this, &handle_key](auto k, auto state_key)
                    {
                        if (keys[k.first].is_up() and k.second.is_down())
                            handle_key({ state_key, not keys[state_key] });

                        ps2->set_leds(keys[key::num_lock_state].is_down(),
                            keys[key::caps_lock_state].is_down(),
                            keys[key::scroll_lock_state].is_down());
                    };

                    switch (k.first)
                    {
                    case key::num_lock: set_lock_state(k, key::num_lock_state); break;
                    case key::caps_lock: set_lock_state(k, key::caps_lock_state); break;
                    case key::scroll_lock: set_lock_state(k, key::scroll_lock_state); break;
                    }
                }
            }
            catch (...)
            {
                if (not async) throw;
                auto e = std::current_exception();
                thread::invoke_main([e] { std::rethrow_exception(e); });
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
