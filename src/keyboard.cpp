/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
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
                    auto& s = keys[k.first];
                    if (s.is_down() and k.second.is_down()) k.second = key_state::repeat;
                    s = k.second;
                    key_changed(k.first, k.second);
                };

                auto handle_virtual_key = [this, &handle_key](key_state_pair k, key vk, std::initializer_list<key> list)
                {
                    bool found { false };
                    key_state state { };
                    for (auto&& i : list)
                    {
                        if (i == k.first) found = true;
                        state |= keys[i];
                    }
                    if (found) handle_key({ vk, state });
                };

                auto set_lock_state = [this, &handle_key](key_state_pair k, key state_key)
                {
                    if (keys[k.first] == key_state::down) handle_key({ state_key, not keys[state_key] });

                    ps2->set_leds(keys[key::num_lock_state],
                                  keys[key::caps_lock_state],
                                  keys[key::scroll_lock_state]);
                };

                while (auto k = ps2->get_scancode())
                {
                    handle_key(*k);

                    handle_virtual_key(*k, key::any_ctrl,  { key::ctrl_left,  key::ctrl_right  });
                    handle_virtual_key(*k, key::any_alt,   { key::alt_left,   key::alt_right   });
                    handle_virtual_key(*k, key::any_shift, { key::shift_left, key::shift_right });
                    handle_virtual_key(*k, key::any_win,   { key::win_left,   key::win_right   });
                    handle_virtual_key(*k, key::any_enter, { key::enter,      key::num_enter   });

                    switch (k->first)
                    {
                    case key::num_lock:    set_lock_state(*k, key::num_lock_state);    break;
                    case key::caps_lock:   set_lock_state(*k, key::caps_lock_state);   break;
                    case key::scroll_lock: set_lock_state(*k, key::scroll_lock_state); break;
                    }
                }
            }
            catch (...)
            {
                if (not async) throw;
                this_thread::invoke_main([e_ptr = new std::exception_ptr { std::current_exception() }]
                    {   // this is horrible
                        auto e { std::move(*e_ptr) };
                        delete e_ptr;
                        std::rethrow_exception(e);
                    });
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

        void keyboard::auto_update(bool enable)
        {
            if (enable) ps2->set_callback([this]() { do_update(true); });
            else ps2->set_callback(nullptr);
        }

        keyboard::keyboard()
        {
            ps2->init_keyboard();
            keys.reserve(128);
        }

        keyboard::~keyboard()
        {
            restore_cin();
            ps2->reset_keyboard();
        }
    }
}
