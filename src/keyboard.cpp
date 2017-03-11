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

                key_changed(*this, k);
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
    }
}