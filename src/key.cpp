/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2016 - 2026 J.W. Jagersma, see COPYING.txt for details    */

#include <jw/io/key.h>
#include <fmt/core.h>

namespace jw::io
{
    static constexpr char ascii_table(key K)
    {
        using enum key;
        switch (K)
        {
        case a:                 return 'a';
        case b:                 return 'b';
        case c:                 return 'c';
        case d:                 return 'd';
        case e:                 return 'e';
        case f:                 return 'f';
        case g:                 return 'g';
        case h:                 return 'h';
        case i:                 return 'i';
        case j:                 return 'j';
        case k:                 return 'k';
        case l:                 return 'l';
        case m:                 return 'm';
        case n:                 return 'n';
        case o:                 return 'o';
        case p:                 return 'p';
        case q:                 return 'q';
        case r:                 return 'r';
        case s:                 return 's';
        case t:                 return 't';
        case u:                 return 'u';
        case v:                 return 'v';
        case w:                 return 'w';
        case x:                 return 'x';
        case y:                 return 'y';
        case z:                 return 'z';
        case n0:                return '0';
        case n1:                return '1';
        case n2:                return '2';
        case n3:                return '3';
        case n4:                return '4';
        case n5:                return '5';
        case n6:                return '6';
        case n7:                return '7';
        case n8:                return '8';
        case n9:                return '9';
        case num_div:           return '/';
        case num_mul:           return '*';
        case num_sub:           return '-';
        case num_add:           return '+';
        case num_enter:         return '\n';
        case backtick:          return '`';
        case minus:             return '-';
        case equals:            return '=';
        case tab:               return '\t';
        case bracket_left:      return '[';
        case bracket_right:     return ']';
        case backslash:         return '\\';
        case semicolon:         return ';';
        case quote:             return '\'';
        case comma:             return ',';
        case dot:               return '.';
        case slash:             return '/';
        case space:             return ' ';
        case enter:             return '\n';
        case backspace:         return '\b';
        default:                return '\0';
        }
    };

    static constexpr char ascii_caps_table(key K)
    {
        using enum key;
        switch (K)
        {
        case a:                 return 'A';
        case b:                 return 'B';
        case c:                 return 'C';
        case d:                 return 'D';
        case e:                 return 'E';
        case f:                 return 'F';
        case g:                 return 'G';
        case h:                 return 'H';
        case i:                 return 'I';
        case j:                 return 'J';
        case k:                 return 'K';
        case l:                 return 'L';
        case m:                 return 'M';
        case n:                 return 'N';
        case o:                 return 'O';
        case p:                 return 'P';
        case q:                 return 'Q';
        case r:                 return 'R';
        case s:                 return 'S';
        case t:                 return 'T';
        case u:                 return 'U';
        case v:                 return 'V';
        case w:                 return 'W';
        case x:                 return 'X';
        case y:                 return 'Y';
        case z:                 return 'Z';
        default:                return '\0';
        }
    }

    static constexpr char ascii_num_table(key K)
    {
        using enum key;
        switch (K)
        {
        case num_0:             return '0';
        case num_1:             return '1';
        case num_2:             return '2';
        case num_3:             return '3';
        case num_4:             return '4';
        case num_5:             return '5';
        case num_6:             return '6';
        case num_7:             return '7';
        case num_8:             return '8';
        case num_9:             return '9';
        case num_dot:           return '.';
        default:                return '\0';
        }
    }

    static constexpr char ascii_shift_table(key K)
    {
        using enum key;
        switch (K)
        {
        case n0:                return ')';
        case n1:                return '!';
        case n2:                return '@';
        case n3:                return '#';
        case n4:                return '$';
        case n5:                return '%';
        case n6:                return '^';
        case n7:                return '&';
        case n8:                return '*';
        case n9:                return '(';
        case backtick:          return '~';
        case minus:             return '_';
        case equals:            return '+';
        case bracket_left:      return '{';
        case bracket_right:     return '}';
        case backslash:         return '|';
        case semicolon:         return ':';
        case quote:             return '\"';
        case comma:             return '<';
        case dot:               return '>';
        case slash:             return '?';
        default:                return '\0';
        }
    }

    static constexpr signed char ascii_ctrl_table(key K)
    {
        using enum key;
        switch (K)
        {
        case n2:                return 0x00;
        case a:                 return 0x01;
        case b:                 return 0x02;
        case c:                 return 0x03;
        case d:                 return 0x04;
        case e:                 return 0x05;
        case f:                 return 0x06;
        case g:                 return 0x07;
        case h:                 return 0x08;
        case i:                 return 0x09;
        case j:                 return 0x0a;
        case k:                 return 0x0b;
        case l:                 return 0x0c;
        case m:                 return 0x0d;
        case n:                 return 0x0e;
        case o:                 return 0x0f;
        case p:                 return 0x10;
        case q:                 return 0x11;
        case r:                 return 0x12;
        case s:                 return 0x13;
        case t:                 return 0x14;
        case u:                 return 0x15;
        case v:                 return 0x16;
        case w:                 return 0x17;
        case x:                 return 0x18;
        case y:                 return 0x19;
        case z:                 return 0x1a;
        case bracket_left:      return 0x1b;
        case backslash:         return 0x1c;
        case bracket_right:     return 0x1d;
        case n6:                return 0x1e;
        case minus:             return 0x1f;
        default:                return -1;
        }
    }

    static constexpr const char* long_name_table(key K)
    {
        using enum key;
        switch (K)
        {
        case esc:               return "Esc";
        case f1:                return "F1";
        case f2:                return "F2";
        case f3:                return "F3";
        case f4:                return "F4";
        case f5:                return "F5";
        case f6:                return "F6";
        case f7:                return "F7";
        case f8:                return "F8";
        case f9:                return "F9";
        case f10:               return "F10";
        case f11:               return "F11";
        case f12:               return "F12";
        case scroll_lock:       return "Scroll Lock";
        case scroll_lock_state: return "Scroll Lock state";
        case num_lock:          return "Num Lock";
        case num_lock_state:    return "Num Lock state";
        case caps_lock:         return "Caps Lock";
        case caps_lock_state:   return "Caps Lock state";
        case shift_left:        return "Left Shift";
        case shift_right:       return "Right Shift";
        case any_shift:         return "Shift";
        case ctrl_left:         return "Left Ctrl";
        case ctrl_right:        return "Right Ctrl";
        case any_ctrl:          return "Ctrl";
        case alt_left:          return "Left Alt";
        case alt_right:         return "Right Alt";
        case any_alt:           return "Alt";
        case win_left:          return "Left Win";
        case win_right:         return "Right Win";
        case any_win:           return "Win";
        case win_menu:          return "Menu";
        case tab:               return "Tab";
        case backspace:         return "Backspace";
        case enter:             return "Enter";
        case any_enter:         return "Enter";
        case space:             return "Space";
        case print_screen:      return "Print Screen";
        case pause:             return "Pause";
        case insert:            return "Insert";
        case del:               return "Delete";
        case home:              return "Home";
        case end:               return "End";
        case page_up:           return "Page Up";
        case page_down:         return "Page Down";
        case up:                return "Up";
        case down:              return "Down";
        case left:              return "Left";
        case right:             return "Right";
        case num_div:           return "Numpad /";
        case num_mul:           return "Numpad *";
        case num_sub:           return "Numpad -";
        case num_add:           return "Numpad +";
        case num_dot:           return "Numpad .";
        case num_enter:         return "Numpad Enter";
        case num_0:             return "Numpad 0";
        case num_1:             return "Numpad 1";
        case num_2:             return "Numpad 2";
        case num_3:             return "Numpad 3";
        case num_4:             return "Numpad 4";
        case num_5:             return "Numpad 5";
        case num_6:             return "Numpad 6";
        case num_7:             return "Numpad 7";
        case num_8:             return "Numpad 8";
        case num_9:             return "Numpad 9";
        case mm_back:           return "Back";
        case mm_forward:        return "Forward";
        case mm_play:           return "Play";
        case mm_pause:          return "Pause";
        case mm_stop:           return "Stop";
        case mm_volume_up:      return "Volume Up";
        case mm_volume_down:    return "Volume Down";
        case web_home:          return "Home";
        case web_favourites:    return "Favourites";
        case web_search:        return "Search";
        case web_mail:          return "Mail";
        case pwr_on:            return "Power On";
        case pwr_sleep:         return "Sleep";
        case pwr_wake:          return "Wake";
        default:                return nullptr;
        }
    }

    static constexpr auto short_name_table = []() consteval
    {
        std::array<char, 0x100> tbl { };
        for (unsigned i = 0; i != tbl.size(); ++i)
        {
            const auto k = static_cast<key>(i);
            if (auto a = ascii_caps_table(k))
                tbl[i] = a;
            else
                tbl[i] = ascii_table(k);
        }
        return tbl;
    }();

    std::optional<char> to_char(key k, modifier_keys mod) noexcept
    {
        if (mod.alt | mod.win)
            return std::nullopt;
        if (mod.ctrl)
        {
            const int a = ascii_ctrl_table(k);
            if (a < 0)
                return std::nullopt;
            return a;
        }

        if (mod.shift xor mod.num_lock)
            if (const char a = ascii_num_table(k))
                return a;
        if (mod.shift xor mod.caps_lock)
            if (const char a = ascii_caps_table(k))
                return a;
        if (mod.shift)
            if (const char a = ascii_shift_table(k))
                return a;
        if (const char a = ascii_table(k))
            return a;
        return std::nullopt;
    }

    std::string_view name(key k)
    {
        if (const char* str = long_name_table(k))
            return { str };

        const unsigned i = static_cast<std::uint16_t>(k);
        if (i < short_name_table.size())
            if (const char& c = short_name_table[i])
                return { &c, 1 };

        static char buf[8];
        return { buf, fmt::format_to(buf, "{:0>4X}", i) };
    }
}
