/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
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
                void enable() { keyb.key_changed += event_handler; }
                void disable() { keyb.key_changed -= event_handler; }

            protected:
                virtual int sync() override;
                virtual std::streamsize xsgetn(char_type* s, std::streamsize n) override;
                virtual int_type underflow() override;

            private:
                callback<bool(key_state_pair)> event_handler { [this](auto k)
                {
                    if (egptr() >= buffer.end()) sync();
                    if (k.second.is_down() && k.first.is_printable(keyb))
                    {
                        auto c = k.first.to_ascii(keyb);
                        *(ptr++) = c;
                        if (echo)
                        {
                            *echo_stream << c << std::flush;
                            if (k.first == key::backspace) *echo_stream << ' ' << c << std::flush;
                        }
                        setg(buffer.begin(), gptr(), ptr);
                        return true;
                    }
                    return false;
                } };

                std::array<char_type, 1_KB> buffer;
                char_type* ptr { buffer.data() };
                keyboard& keyb;
            };
        }
    }
}
