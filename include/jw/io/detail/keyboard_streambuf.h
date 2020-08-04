/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <functional>
#include <array>
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
                bool event_handler(key k, key_state s);

                std::array<char_type, 1_KB> buffer;
                std::uint32_t alt_sequence { 0x80000000 };
                char_type* ptr { buffer.data() };
                keyboard& keyb;
            };
        }
    }
}
