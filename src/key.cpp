/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#include <optional>
#include <jw/io/key.h>
#include <jw/io/keyboard.h>

namespace jw
{
    namespace io
    {
        template<typename M, typename K>
        static inline const typename M::mapped_type* find(const M& map, const K& key) noexcept
        {
            auto i = map.find(key);
            if (i != map.cend()) return &i->second;
            else return nullptr;
        };

        char key::to_ascii(bool shift, bool capslock, bool numlock) const
        {
            if (shift xor numlock) if (auto a = find(ascii_num_table, value)) return *a;
            if (shift xor capslock) if (auto a = find(ascii_caps_table, value)) return *a;
            if (shift) if (auto a = find(ascii_shift_table, value)) return *a;
            if (auto a = find(ascii_table, value)) return *a;
            return 0;
        }

        std::string_view key::name() const
        {
            if (auto a = find(name_table, value)) return *a;

            auto sv = [] (const auto& a) { return std::string_view { a, 1 }; };
            if (auto a = find(ascii_num_table, value)) return sv(a);
            if (auto a = find(ascii_caps_table, value)) return sv(a);
            if (auto a = find(ascii_table, value)) return sv(a);

            return "Unknown";
        }

        char key::to_ascii(const keyboard& kb) const
        {
            return key::to_ascii(kb.get(any_shift),
                                 kb.get(caps_lock_state),
                                 kb.get(num_lock_state));
        }

        bool key::is_printable(bool shift, bool capslock, bool numlock) const
        {
            return to_ascii(shift, capslock, numlock) != 0;
        }

        bool key::is_printable(const keyboard& kb) const
        {
            return to_ascii(kb) != 0; // can't inline this :(
        }

        std::unordered_map<key, char> key::ascii_table
        {
            { key::a, 'a' },
            { key::b, 'b' },
            { key::c, 'c' },
            { key::d, 'd' },
            { key::e, 'e' },
            { key::f, 'f' },
            { key::g, 'g' },
            { key::h, 'h' },
            { key::i, 'i' },
            { key::j, 'j' },
            { key::k, 'k' },
            { key::l, 'l' },
            { key::m, 'm' },
            { key::n, 'n' },
            { key::o, 'o' },
            { key::p, 'p' },
            { key::q, 'q' },
            { key::r, 'r' },
            { key::s, 's' },
            { key::t, 't' },
            { key::u, 'u' },
            { key::v, 'v' },
            { key::w, 'w' },
            { key::x, 'x' },
            { key::y, 'y' },
            { key::z, 'z' },
            { key::n0, '0' },
            { key::n1, '1' },
            { key::n2, '2' },
            { key::n3, '3' },
            { key::n4, '4' },
            { key::n5, '5' },
            { key::n6, '6' },
            { key::n7, '7' },
            { key::n8, '8' },
            { key::n9, '9' },
            { key::num_div, '/' },
            { key::num_mul, '*' },
            { key::num_sub, '-' },
            { key::num_add, '+' },
            { key::num_enter, '\n' },
            { key::backtick, '`' },
            { key::minus, '-' },
            { key::equals, '=' },
            { key::tab, '\t' },
            { key::brace_left, '[' },
            { key::brace_right, ']' },
            { key::backslash, '\\' },
            { key::semicolon, ';' },
            { key::quote, '\'' },
            { key::comma, ',' },
            { key::dot, '.' },
            { key::slash, '/' },
            { key::space, ' ' },
            { key::enter, '\n' },
            { key::backspace, '\b' }
        };

        std::unordered_map<key, char> key::ascii_caps_table
        {
            { key::a, 'A' },
            { key::b, 'B' },
            { key::c, 'C' },
            { key::d, 'D' },
            { key::e, 'E' },
            { key::f, 'F' },
            { key::g, 'G' },
            { key::h, 'H' },
            { key::i, 'I' },
            { key::j, 'J' },
            { key::k, 'K' },
            { key::l, 'L' },
            { key::m, 'M' },
            { key::n, 'N' },
            { key::o, 'O' },
            { key::p, 'P' },
            { key::q, 'Q' },
            { key::r, 'R' },
            { key::s, 'S' },
            { key::t, 'T' },
            { key::u, 'U' },
            { key::v, 'V' },
            { key::w, 'W' },
            { key::x, 'X' },
            { key::y, 'Y' },
            { key::z, 'Z' }
        };

        std::unordered_map<key, char> key::ascii_num_table
        {
            { key::num_0, '0' },
            { key::num_1, '1' },
            { key::num_2, '2' },
            { key::num_3, '3' },
            { key::num_4, '4' },
            { key::num_5, '5' },
            { key::num_6, '6' },
            { key::num_7, '7' },
            { key::num_8, '8' },
            { key::num_9, '9' },
            { key::num_dot, '.' }
        };

        std::unordered_map<key, char> key::ascii_shift_table
        {
            { key::n0, ')' },
            { key::n1, '!' },
            { key::n2, '@' },
            { key::n3, '#' },
            { key::n4, '$' },
            { key::n5, '%' },
            { key::n6, '^' },
            { key::n7, '&' },
            { key::n8, '*' },
            { key::n9, '(' },
            { key::backtick, '~' },
            { key::minus, '_' },
            { key::equals, '+' },
            { key::brace_left, '{' },
            { key::brace_right, '}' },
            { key::backslash, '|' },
            { key::semicolon, ':' },
            { key::quote, '\"' },
            { key::comma, '<' },
            { key::dot, '>' },
            { key::slash, '?' }
        };

        std::unordered_map<key, std::string_view> key::name_table
        {
            { key::esc, "Esc" },
            { key::f1, "F1" },
            { key::f2, "F2" },
            { key::f3, "F3" },
            { key::f4, "F4" },
            { key::f5, "F5" },
            { key::f6, "F6" },
            { key::f7, "F7" },
            { key::f8, "F8" },
            { key::f9, "F9" },
            { key::f10, "F10" },
            { key::f11, "F11" },
            { key::f12, "F12" },
            { key::scroll_lock, "Scroll Lock" },
            { key::num_lock, "Num Lock" },
            { key::caps_lock, "Caps Lock" },
            { key::shift_left, "Left Shift" },
            { key::shift_right, "Right Shift" },
            { key::any_shift, "Shift" },
            { key::ctrl_left, "Left Ctrl" },
            { key::ctrl_right, "Right Ctrl" },
            { key::any_ctrl, "Ctrl" },
            { key::alt_left, "Left Alt" },
            { key::alt_right, "Right Alt" },
            { key::any_alt, "Alt" },
            { key::win_left, "Left Win" },
            { key::win_right, "Right Win" },
            { key::any_win, "Win" },
            { key::win_menu, "Menu" },
            { key::tab, "Tab" },
            { key::backspace, "Backspace" },
            { key::enter, "Enter" },
            { key::any_enter, "Enter" },
            { key::space, "Space" },
            { key::print_screen, "Print Screen" },
            { key::pause, "Pause" },
            { key::insert, "Insert" },
            { key::del, "Delete" },
            { key::home, "Home" },
            { key::end, "End" },
            { key::page_up, "Page Up" },
            { key::page_down, "Page Down" },
            { key::up, "Up" },
            { key::down, "Down" },
            { key::left, "Left" },
            { key::right, "Right" },
            { key::num_div, "Numpad /" },
            { key::num_mul, "Numpad *" },
            { key::num_sub, "Numpad -" },
            { key::num_add, "Numpad +" },
            { key::num_dot, "Numpad ." },
            { key::num_enter, "Numpad Enter" },
            { key::num_0, "Numpad 0" },
            { key::num_1, "Numpad 1" },
            { key::num_2, "Numpad 2" },
            { key::num_3, "Numpad 3" },
            { key::num_4, "Numpad 4" },
            { key::num_5, "Numpad 5" },
            { key::num_6, "Numpad 6" },
            { key::num_7, "Numpad 7" },
            { key::num_8, "Numpad 8" },
            { key::num_9, "Numpad 9" },
            { key::mm_back, "Back" },
            { key::mm_forward, "Forward" },
            { key::mm_play, "Play" },
            { key::mm_pause, "Pause" },
            { key::mm_stop, "Stop" },
            { key::mm_volume_up, "Volume Up" },
            { key::mm_volume_down, "Volume Down" },
            { key::web_home, "Home" },
            { key::web_favourites, "Favourites" },
            { key::web_search, "Search" },
            { key::web_mail, "Mail" },
            { key::pwr_on, "Power On" },
            { key::pwr_sleep, "Sleep" },
            { key::pwr_wake, "Wake" }
        };
    }
}
