/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/io/keyboard.h>

namespace jw
{
    namespace io
    {
        namespace detail
        {
            struct keyboard_streambuf : public std::streambuf
            {
                friend class jw::io::keyboard;

                keyboard_streambuf(keyboard& kb) : keyb(kb)
                {
                    setp(nullptr, nullptr);
                    setg(buffer.data(), buffer.data(), buffer.data());
                }

                bool echo { true };
                std::ostream* echo_stream { &std::cout };
                void enable() { keyb.key_changed += event_callback; }
                void disable() { keyb.key_changed -= event_callback; }

            protected:
                virtual int sync() override;
                virtual std::streamsize xsgetn(char_type* s, std::streamsize n) override;
                virtual int_type underflow() override;

            private:
                callback<bool(key, key_state)> event_callback { std::bind_front(&keyboard_streambuf::event_handler, this) };
                bool event_handler(key k, key_state s)
                {
                    auto insert = [this](char c)
                    {
                        *(ptr++) = c;
                        if (echo)
                        {
                            *echo_stream << c;
                            if (c == '\b')* echo_stream << " \b";
                            *echo_stream << std::flush;
                        }
                        setg(buffer.begin(), gptr(), ptr);
                    };

                    if (egptr() >= buffer.end()) sync();

                    if (k == key::any_alt and s == key_state::up)
                    {
                        if (alt_sequence <= 0xff) insert(alt_sequence);
                        alt_sequence = 0x80000000;
                        return true;
                    }

                    if (s.is_up()) return false;

                    if (keyb[key::any_alt])
                    {
                        if (not (keyb[key::any_shift] xor keyb[key::num_lock_state])) return false;
                        if (keyb[key::any_ctrl]) return false;
                        auto n = [k]
                        {
                            switch (k)
                            {
                            case key::num_0: return 0;
                            case key::num_1: return 1;
                            case key::num_2: return 2;
                            case key::num_3: return 3;
                            case key::num_4: return 4;
                            case key::num_5: return 5;
                            case key::num_6: return 6;
                            case key::num_7: return 7;
                            case key::num_8: return 8;
                            case key::num_9: return 9;
                            default: return -1;
                            }
                        }();
                        if (n == -1) return false;
                        alt_sequence *= 10;
                        alt_sequence += n;
                        return true;
                    }

                    if (auto c = k.to_ascii(keyb))
                    {
                        insert(c);
                        return true;
                    }
                    return false;
                }

                std::array<char_type, 1_KB> buffer;
                std::uint32_t alt_sequence { 0x80000000 };
                char_type* ptr { buffer.data() };
                keyboard& keyb;
            };
        }
    }
}
