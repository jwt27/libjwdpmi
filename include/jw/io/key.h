/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2023 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <string_view>
#include <unordered_map>
#include <jw/enum_struct.h>

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

    struct key : public enum_struct<std::uint_fast16_t>
    {
        using E = enum_struct<std::uint_fast16_t>;
        using T = typename E::underlying_type;
        enum : T
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

            // 00C0 - 00FF = virtual keys
            any_shift = 0xC0, any_ctrl, any_alt, any_win, any_enter,
            num_lock_state, caps_lock_state, scroll_lock_state,

            // 0100 - 01FF = undefined keys
            // E000 - E1FF = undefined set2 extended keys
        };
        using E::E;
        using E::operator=;

        constexpr key(const key&) = default;
        constexpr key& operator=(const key&) = default;

        char to_ascii(modifier_keys) const;
        bool is_virtual() const noexcept { return value >= 0xC0 and value < 0x100; }
        std::string_view name() const;

    private:
        static const std::unordered_map<key, char> ascii_table;
        static const std::unordered_map<key, char> ascii_num_table;
        static const std::unordered_map<key, char> ascii_caps_table;
        static const std::unordered_map<key, char> ascii_shift_table;
        static const std::unordered_map<key, char> ascii_ctrl_table;
        static std::unordered_map<key, std::string> name_table;
    };


    struct key_state : public enum_struct<std::uint_fast8_t>
    {
        using E = enum_struct<std::uint_fast8_t>;
        using T = typename E::underlying_type;
        enum : T
        {
            up = 0b00,
            down = 0b01,
            repeat = 0b11
        };
        using E::E;
        using E::operator=;

        constexpr key_state(const key_state&) = default;
        constexpr key_state& operator=(const key_state&) = default;

        constexpr key_state& operator|=(const key_state& other) { this->value |= other.value; return *this; }

        constexpr bool is_up() const noexcept { return value == up; }
        constexpr bool is_down() const noexcept { return value & 1; }
        constexpr explicit operator bool() const noexcept { return is_down(); }
    };

    using key_state_pair = std::pair<key, key_state>;
}
