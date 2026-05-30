/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2016 - 2026 J.W. Jagersma, see COPYING.txt for details    */

#include <exception>
#include <jw/io/keyboard.h>
#include <jw/io/detail/keyboard_streambuf.h>
#include <jw/dpmi/bda.h>

namespace jw::io
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
            auto handle_event = [&](key_event& e)
            {
                auto bit = keys(e.key);
                e.repeat = e.pressed == bit;
                bit = e.pressed;
                key_changed(e);
            };

            auto handle_virtual_key = [&](key k, key vk, auto... list)
            {
                if (((k != list) and ...))
                    return;

                key_event e { };
                e.key = vk;
                e.pressed = (keys(list) | ...);
                handle_event(e);
            };

            auto toggle_lock_state = [&](key vk)
            {
                key_event e { };
                e.key = vk;
                e.pressed = not keys(vk);
                handle_event(e);

                const bool scroll = keys(key::scroll_lock_state);
                const bool num = keys(key::num_lock_state);
                const bool caps = keys(key::caps_lock_state);

                ps2->set_leds(num, caps, scroll);

                bda_kb_flags flags { dpmi::bda->read<std::byte>(0x17) };
                flags.scroll_lock = scroll;
                flags.num_lock = num;
                flags.caps_lock = caps;
                dpmi::bda->write<std::byte>(0x17, flags);
            };

            while (auto e = ps2->get_scancode())
            {
                handle_event(*e);

                handle_virtual_key(e->key, key::any_ctrl,  key::ctrl_left,  key::ctrl_right );
                handle_virtual_key(e->key, key::any_alt,   key::alt_left,   key::alt_right  );
                handle_virtual_key(e->key, key::any_shift, key::shift_left, key::shift_right);
                handle_virtual_key(e->key, key::any_win,   key::win_left,   key::win_right  );
                handle_virtual_key(e->key, key::any_enter, key::enter,      key::num_enter  );

                if (e->pressed & not e->repeat) switch (e->key)
                {
                case key::num_lock:    toggle_lock_state(key::num_lock_state);    break;
                case key::caps_lock:   toggle_lock_state(key::caps_lock_state);   break;
                case key::scroll_lock: toggle_lock_state(key::scroll_lock_state); break;
                default: /* silence stupid warning */ break;
                }
            }
        }
        catch (...)
        {
            if (not async)
                throw;

            this_thread::invoke_main([e = std::current_exception()]
            {
                std::rethrow_exception(e);
            });
        }
    }

    modifier_keys keyboard::modifiers() const noexcept
    {
        return
        {
            .ctrl      = get(key::any_ctrl),
            .alt       = get(key::any_alt),
            .shift     = get(key::any_shift),
            .win       = get(key::any_win),
            .num_lock  = get(key::num_lock_state),
            .caps_lock = get(key::caps_lock_state)
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
