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
#include <unordered_map>
#include <memory>
#include <istream>

#include <jw/io/key.h>
#include <jw/io/detail/scancode.h>
#include <jw/io/keyboard_interface.h>
#include <jw/thread/mutex.h>
#include <jw/event.h>

namespace jw
{
    namespace io
    {
        struct keyboard final
        {
            event<void(key_state_pair)> key_changed;

            const key_state& get(key k) const { return keys[k]; }
            const key_state& operator[](key k) const { return keys[k]; }

            void update();

            void auto_update(bool enable)
            {
                if (enable) interface->set_keyboard_update_thread({ [this]() { update(); } });
                else interface->set_keyboard_update_thread({ });
            }

            keyboard(std::shared_ptr<keyboard_interface> intf) : interface(intf) { }

        private:
            std::shared_ptr<keyboard_interface> interface;
            mutable std::unordered_map<key, key_state> keys { };
        };

        namespace detail
        {
            struct keyboard_streambuf : public std::streambuf   // TODO: echo characters
            {
                keyboard_streambuf(keyboard& kb) : keyb(kb)
                {
                    keyb.key_changed += event_handler;
                    setp(nullptr, nullptr);
                    setg(buffer.data(), buffer.data(), buffer.data());
                }

            protected:
                virtual int sync() override;
                virtual std::streamsize xsgetn(char_type* s, std::streamsize n) override;
                virtual int_type underflow() override;

            private:
                callback<void(key_state_pair)> event_handler { [this](auto k)
                {
                    if (egptr() >= buffer.data() + buffer.size()) sync();
                    if (k.second.is_down() && k.first.is_printable(keyb))
                        *(ptr++) = k.first.to_ascii(keyb);
                    setg(buffer.begin(), gptr(), ptr);
                } };

                std::array<char_type, 1_KB> buffer;
                char_type* ptr { buffer.data() };
                keyboard& keyb;
            };
        }

        struct keyboard_istream : public std::istream
        {
            keyboard_istream(keyboard& kb) : std::istream(&streambuf), streambuf(kb) { }

        private:
            detail::keyboard_streambuf streambuf;
        };
    }
}
