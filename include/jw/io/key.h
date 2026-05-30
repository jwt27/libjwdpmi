/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2016 - 2026 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <span>

namespace jw::io
{
    class keyboard;

    struct modifier_keys
    {
        bool ctrl       : 1;
        bool alt        : 1;
        bool shift      : 1;
        bool win        : 1;
        bool num_lock   : 1;
        bool caps_lock  : 1;
    };

    enum class key : std::uint_fast16_t
    {
        // 0000 - 00BF = defined keys
        bad_key = 0,

        esc, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12,
        backtick, n1, n2, n3, n4, n5, n6, n7, n8, n9, n0, minus, equals, backspace,
        tab, q, w, e, r, t, y, u, i, o, p, bracket_left, bracket_right, backslash,
        caps_lock, a, s, d, f, g, h, j, k, l, semicolon, quote, enter,
        shift_left, z, x, c, v, b, n, m, comma, dot, slash, shift_right,
        ctrl_left, alt_left, space, alt_right, ctrl_right,

        print_screen, scroll_lock, pause,
        insert, home, page_up,
        del, end, page_down,
        up, left, down, right,

        num_lock, num_div, num_mul, num_sub, num_add,
        num_7, num_8, num_9,
        num_4, num_5, num_6,
        num_1, num_2, num_3,
        num_0, num_dot, num_enter,

        win_left, win_right, win_menu,
        mm_back, mm_forward, mm_play, mm_pause, mm_stop,
        mm_volume_up, mm_volume_down,
        web_home, web_favourites, web_search, web_mail,
        pwr_on, pwr_sleep, pwr_wake,

        // Aliases
        hyphen = minus,
        dash = minus,
        plus = equals,
        brace_left = bracket_left,
        brace_right = bracket_right,
        colon = semicolon,
        apostrophe = quote,
        period = dot,

        // 00C0 - 00FF = virtual keys
        any_enter = 0xC0, any_ctrl, any_alt, any_shift, any_win,
        num_lock_state, caps_lock_state, scroll_lock_state,

        // 0100 - 01FF = undefined keys
        // E000 - E1FF = undefined set2 extended keys
    };

    // Turn a keystroke into its associated character in the US/International
    // keyboard layout.
    char to_char(key, modifier_keys);

    // Return a human-readable name for each key, eg. "Numpad Enter" for
    // key::num_enter.  For undefined keys, returns the key code as a hex
    // string, eg. "015d".
    std::string_view name(key);

    // Check if a key is virtual.
    constexpr bool is_virtual(key k) noexcept
    {
        return static_cast<unsigned>(k) - 0xc0u < (0x100u - 0xc0u);
    }

    struct key_event
    {
        io::key key;
        bool pressed;
        bool repeat;
        std::uint8_t scancode_set;
        std::span<std::uint8_t> scancode;
    };
}
