/******************************* libjwdpmi **********************************
Copyright (C) 2016-2017  J.W. Jagersma

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

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
                    keyb.key_changed += event_handler;
                    setp(nullptr, nullptr);
                    setg(buffer.data(), buffer.data(), buffer.data());
                }                                                       

                bool echo { true };
                std::ostream* echo_stream { &std::cout };

            protected:
                virtual int sync() override;
                virtual std::streamsize xsgetn(char_type* s, std::streamsize n) override;
                virtual int_type underflow() override;

            private:
                callback<void(key_state_pair)> event_handler { [this](auto k)
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
                    }
                    setg(buffer.begin(), gptr(), ptr);
                } };

                std::array<char_type, 1_KB> buffer;
                char_type* ptr { buffer.data() };
                keyboard& keyb;
            };
        }
    }
}
