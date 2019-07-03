/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <string_view>
#include <unordered_map>
#include <jw/enum_struct.h>

namespace jw
{
    namespace io
    {
        class keyboard;

        struct key : public enum_struct<std::uint_fast16_t>
        {
            using E = enum_struct<std::uint_fast16_t>;
            using T = typename E::underlying_type;
            enum : T
            {
                // 0000 - 00FF = defined keys
                bad_key = 0,
                esc, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12,
                backtick, n1, n2, n3, n4, n5, n6, n7, n8, n9, n0, minus, equals, backspace,
                tab, q, w, e, r, t, y, u, i, o, p, brace_left, brace_right, backslash,
                caps_lock, a, s, d, f, g, h, j, k, l, semicolon, quote, enter,
                shift_left, z, x, c, v, b, n, m, comma, dot, slash, shift_right,
                ctrl_left, alt_left, space, alt_right, ctrl_right,

                print_screen, scroll_lock, pause,
                insert, home, page_up,
                del, end, page_down,
                up, left, down, right,

                num_lock, num_div, num_mul, num_sub,
                num_7, num_8, num_9,
                num_4, num_5, num_6, num_add,
                num_1, num_2, num_3,
                num_0, num_dot, num_enter,

                win_left, win_right, win_menu,
                mm_back, mm_forward, mm_play, mm_pause, mm_stop,
                mm_volume_up, mm_volume_down,
                web_home, web_favourites, web_search, web_mail,
                pwr_on, pwr_sleep, pwr_wake,
                // TODO: 122-key ?

                // 0100 - 01FF = undefined keys

                // 0200 - 02FF = virtual keys
                any_shift = 0x200, any_ctrl, any_alt, any_win, any_enter,
                num_lock_state = 0x280, caps_lock_state, scroll_lock_state,

                // E000 - E1FF = undefined set2 extended keys
            };
            using E::E;
            using E::operator=;

            constexpr key(const key&) = default;
            constexpr key& operator=(const key&) = default;

            char to_ascii(bool shift, bool caps_lock, bool num_lock) const;
            char to_ascii(const keyboard& kb) const;
            bool is_printable(bool shift, bool caps_lock, bool num_lock) const;
            bool is_printable(const keyboard& kb) const;
            bool is_virtual() const noexcept { return value >= 0x200 and value < 0x300; }
            std::string_view name() const;

        private:
            static std::unordered_map<key, char> ascii_table;
            static std::unordered_map<key, char> ascii_num_table;
            static std::unordered_map<key, char> ascii_caps_table;
            static std::unordered_map<key, char> ascii_shift_table;
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
            constexpr bool is_down() const noexcept { return not is_up(); }
            constexpr explicit operator bool() const noexcept { return is_down(); }
        };

        using key_state_pair = std::pair<key, key_state>;
    }
}

ENUM_STRUCT_SPECIALIZE_STD_HASH(jw::io::key);
ENUM_STRUCT_SPECIALIZE_STD_HASH(jw::io::key_state);
