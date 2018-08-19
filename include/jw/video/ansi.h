/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */

#pragma once

#include <jw/dpmi/realmode.h>
#include <jw/vector.h>
#include <cstdint>
#include <iostream>
#include <utility>
#include <tuple>
#include <cmath>
#include <charconv>

namespace jw::video::ansi
{
    inline bool install_check()
    {
        dpmi::realmode_registers r { };
        r.ax = 0x1a00;
        r.call_int(0x2f);
        return r.al == 0xff;
    }

    template<typename... T>
    struct ansi_code
    {
        static constexpr char esc = 27;

        std::tuple<char, char, std::decay_t<T>...> string;

        constexpr ansi_code(T... args) noexcept : string(std::make_tuple(esc, '[', args...)) { }
        constexpr ansi_code(const ansi_code&) noexcept = default;
        constexpr ansi_code(ansi_code&&) noexcept = default;

        template<typename... U>
        constexpr ansi_code(std::tuple<U...> t) noexcept : string(t) { }

        friend std::ostream& operator<<(std::ostream& out, const ansi_code& c)
        {
            std::array<char, tuple_size() * 4> str;
            c.emit(std::make_index_sequence<tuple_size()> { }, str.data());
            return out << str.data();
        }

        template<typename... U>
        friend constexpr auto operator+(ansi_code lhs, ansi_code<U...> rhs) noexcept
        {
            if (std::get<tuple_size() - 1>(lhs.string) == 'm' and std::get<rhs.first_char()>(rhs.string) == 'm')
            {
                std::get<tuple_size() - 1>(lhs.string) = ';';
                std::get<0>(rhs.string) = '\0';
                std::get<1>(rhs.string) = '\0';
            }
            return ansi_code<T..., char, char, U...> { std::tuple_cat(lhs.string, rhs.string) };
        }

        constexpr static std::size_t tuple_size() noexcept { return std::tuple_size_v<decltype(string)>; }

        template<std::size_t i>
        constexpr static bool is_char() noexcept { return std::is_same_v<std::tuple_element_t<i, decltype(string)>, char>; };

        constexpr static std::size_t first_char() noexcept { return first_char(std::make_index_sequence<tuple_size()> { }); }

        template<std::size_t... is>
        constexpr static std::size_t first_char(std::index_sequence<is...>) noexcept { return first_char<is...>(); }

        template<std::size_t i, std::size_t... next>
        constexpr static std::size_t first_char() noexcept
        {
            if constexpr (i > 1 and is_char<i>()) return i;
            else return first_char<next...>();
        };

        template<std::size_t... is>
        void emit(std::index_sequence<is...>, char* out) const { emit<is...>(out); }

        template<std::size_t i, std::size_t... next>
        void emit(char* out) const
        {
            auto j = std::get<i>(string);

            if constexpr (not is_char<i>())
            {
                if (__builtin_expect(j != 0, true))
                {
                    out = std::to_chars(out, out + 4, j).ptr;
                }
                if constexpr (not is_char<i + 1>()) *(out++) = ';';
            }
            else if (__builtin_expect(j != 0, true)) *(out++) = j;

            if constexpr (sizeof...(next) > 0) emit<next...>(out);
            else *out = '\0';
        }
    };

    enum color : std::uint32_t { black, red, green, yellow, blue, magenta, cyan, white };

    constexpr auto reset() noexcept                 { return ansi_code { 0, 'm' }; }
    constexpr auto bold(bool enable) noexcept       { return ansi_code { enable ? 1 : 22, 'm' }; }
    constexpr auto underline(bool enable) noexcept  { return ansi_code { enable ? 4 : 24, 'm' }; }
    constexpr auto blink(bool enable) noexcept      { return ansi_code { enable ? 5 : 25, 'm' }; }
    constexpr auto fast_blink(bool enable) noexcept { return ansi_code { enable ? 6 : 26, 'm' }; }
    constexpr auto reverse(bool enable) noexcept    { return ansi_code { enable ? 7 : 27, 'm' }; }
    constexpr auto invisible(bool enable) noexcept  { return ansi_code { enable ? 8 : 28, 'm' }; }
    constexpr auto fg(color c) noexcept             { return ansi_code { 30 + c, 'm' }; }
    constexpr auto bg(color c) noexcept             { return ansi_code { 40 + c, 'm' }; }

    constexpr auto cursor_up(std::uint32_t p) noexcept      { return ansi_code { p, 'A' }; }
    constexpr auto cursor_down(std::uint32_t p) noexcept    { return ansi_code { p, 'B' }; }
    constexpr auto cursor_right(std::uint32_t p) noexcept   { return ansi_code { p, 'C' }; }
    constexpr auto cursor_left(std::uint32_t p) noexcept    { return ansi_code { p, 'D' }; }

    constexpr auto save_cursor_pos() noexcept       { return ansi_code { 's' }; }
    constexpr auto restore_cursor_pos() noexcept    { return ansi_code { 'u' }; }

    constexpr auto set_cursor(vector2i pos) noexcept    { return ansi_code { pos[1] + 1, pos[0] + 1, 'H' }; }
    constexpr auto move_cursor(vector2i pos) noexcept
    {
        auto x = pos[0], y = pos[1];
        return (x < 0 ? cursor_left(std::abs(x)) : cursor_right(x))
             + (y < 0 ? cursor_up(std::abs(y))   : cursor_down(y));
    }

    constexpr auto clear_screen() noexcept                  { return ansi_code { 2, 'J' }; }
    constexpr auto clear_line() noexcept                    { return ansi_code { 'K' }; }
    constexpr auto insert_lines(std::uint32_t n) noexcept   { return ansi_code { n, 'L' }; }
    constexpr auto remove_lines(std::uint32_t n) noexcept   { return ansi_code { n, 'M' }; }
    constexpr auto insert_spaces(std::uint32_t n) noexcept  { return ansi_code { n, '@' }; }
    constexpr auto erase_chars(std::uint32_t n) noexcept    { return ansi_code { n, 'P' }; }

    constexpr auto set_video_mode(std::uint32_t mode) noexcept  { return ansi_code { '=', mode, 'h' }; }
    constexpr auto set_80x25_mode() noexcept                    { return set_video_mode(3); }
    constexpr auto set_80x43_mode() noexcept                    { return set_80x25_mode() + set_video_mode(43); }
    constexpr auto set_80x50_mode() noexcept                    { return set_80x43_mode() + set_video_mode(50); }

    constexpr auto line_wrap(bool enable) noexcept      { return ansi_code { '?',  7, enable ? 'h' : 'l' }; }
    constexpr auto fast_scroll(bool enable) noexcept    { return ansi_code { '?', 98, enable ? 'h' : 'l' }; }
}
