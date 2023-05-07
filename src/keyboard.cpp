/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2023 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#include <exception>
#include <jw/io/keyboard.h>
#include <jw/io/detail/keyboard_streambuf.h>
#include <jw/dpmi/bda.h>

namespace jw
{
    namespace io
    {
        struct bda_kb_flags
        {
            bool right_shift : 1;
            bool left_shift : 1;
            bool ctrl : 1;
            bool alt : 1;
            bool scroll_lock : 1;
            bool num_lock : 1;
            bool caps_lock : 1;
            bool insert : 1;

            constexpr bda_kb_flags(std::byte b) noexcept { *this = std::bit_cast<bda_kb_flags>(b); }
            constexpr operator std::byte() noexcept { return std::bit_cast<std::byte>(*this); }
        };

        void keyboard::do_update(bool async)
        {
            try
            {
                auto handle_key = [this](key_state_pair k)
                {
                    auto& s = keys(k.first);
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
                        state |= keys(i);
                    }
                    if (found) handle_key({ vk, state });
                };

                auto set_lock_state = [this, &handle_key](key_state_pair k, key state_key)
                {
                    if (keys(k.first) == key_state::down) handle_key({ state_key, not keys(state_key) });

                    ps2->set_leds(keys(key::num_lock_state),
                                  keys(key::caps_lock_state),
                                  keys(key::scroll_lock_state));

                    bda_kb_flags flags { dpmi::bda->read<std::byte>(0x17) };
                    flags.scroll_lock = keys(key::scroll_lock_state);
                    flags.num_lock = keys(key::num_lock_state);
                    flags.caps_lock = keys(key::caps_lock_state);
                    dpmi::bda->write<std::byte>(0x17, flags);
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
                this_thread::invoke_main([e = std::current_exception()] { std::rethrow_exception(e); });
            }
        }

        modifier_keys keyboard::modifiers() const noexcept
        {
            return
            {
                .ctrl      = static_cast<bool>(get(key::any_ctrl)),
                .alt       = static_cast<bool>(get(key::any_alt)),
                .shift     = static_cast<bool>(get(key::any_shift)),
                .win       = static_cast<bool>(get(key::any_win)),
                .num_lock  = static_cast<bool>(get(key::num_lock_state)),
                .caps_lock = static_cast<bool>(get(key::caps_lock_state))
            };
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
            const bda_kb_flags flags { dpmi::bda->read<std::byte>(0x17) };
            keys(key::scroll_lock_state) = flags.scroll_lock;
            keys(key::num_lock_state) = flags.num_lock;
            keys(key::caps_lock_state) = flags.caps_lock;
        }

        keyboard::~keyboard()
        {
            restore_cin();
            ps2->reset_keyboard();
        }
    }
}
