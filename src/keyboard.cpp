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

#include <iterator>
#include <algorithm>

#include <jw/io/keyboard.h>
#include <jw/io/ps2_interface.h>
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

        namespace detail
        {
            int keyboard_streambuf::sync()
            {
                std::copy(gptr(), ptr, buffer.begin());
                ptr = buffer.begin() + (ptr - gptr());
                setg(buffer.begin(), buffer.begin(), ptr);
                thread::yield();
                return 0;
            }

            std::streamsize keyboard_streambuf::xsgetn(char_type * s, std::streamsize n)
            {
                std::streamsize max_n = 0;
                while (max_n < n && underflow() != traits_type::eof()) max_n = std::min(egptr() - gptr(), n);
                std::copy_n(gptr(), max_n, s);
                setg(buffer.begin(), gptr() + max_n, egptr());
                return max_n;
            }

            keyboard_streambuf::int_type keyboard_streambuf::underflow()
            {
                thread::yield_while([this] { return gptr() == egptr(); });
                return *gptr();
            }
        }
    }
}
