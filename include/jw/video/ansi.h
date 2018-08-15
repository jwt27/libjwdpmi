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

namespace jw::video::ansi
{
    bool install_check()
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
            auto f = out.flags();
            out << std::dec;
            c.emit(std::make_index_sequence<tuple_size()> { }, out);
            out.flags(f);
            return out;
        }

        template<typename... U>
        friend constexpr auto operator+(ansi_code lhs, ansi_code<U...> rhs)
        {
            if (std::get<tuple_size() - 1>(lhs.string) == 'm' and std::get<rhs.first_char()>(rhs.string) == 'm')
            {
                std::get<tuple_size() - 1>(lhs.string) = ';';
                std::get<0>(rhs.string) = '\0';
                std::get<1>(rhs.string) = '\0';
            }
            return ansi_code<T..., char, char, U...> { std::tuple_cat(lhs.string, rhs.string) };
        }

        constexpr static std::size_t tuple_size() { return std::tuple_size_v<decltype(string)>; }

        template<std::size_t i>
        constexpr static bool is_char()
        {
            return std::is_same_v<std::tuple_element_t<i, decltype(string)>, char>;
        };

        constexpr static std::size_t first_char() { return first_char(std::make_index_sequence<tuple_size()> { }); }

        template<std::size_t... is>
        constexpr static std::size_t first_char(std::index_sequence<is...>) { return first_char<is...>(); }

        template<std::size_t i, std::size_t... next>
        constexpr static std::size_t first_char()
        {
            if constexpr (i > 1 and is_char<i>()) return i;
            else return first_char<next...>();
        };

        template<std::size_t... is>
        void emit(std::index_sequence<is...>, std::ostream& out) const { emit<is...>(out); }

        template<std::size_t i, std::size_t... next>
        void emit(std::ostream& out) const
        {
            auto j = std::get<i>(string);
            if (j != 0) out << j;
            if constexpr (not is_char<i>()) if constexpr (not is_char<i + 1>()) out << ';';
            if constexpr (sizeof...(next) > 0) emit<next...>(out);
        }
    };

    enum color : std::uint32_t { black, red, green, yellow, blue, magenta, cyan, white };

    auto reset()                    { return ansi_code { 0, 'm' }; }
    auto bold(bool enable)          { return ansi_code { enable ? 1 : 22, 'm' }; }
    auto underline(bool enable)     { return ansi_code { enable ? 4 : 24, 'm' }; }
    auto blink(bool enable)         { return ansi_code { enable ? 5 : 25, 'm' }; }
    auto fast_blink(bool enable)    { return ansi_code { enable ? 6 : 26, 'm' }; }
    auto reverse(bool enable)       { return ansi_code { enable ? 7 : 27, 'm' }; }
    auto invisible(bool enable)     { return ansi_code { enable ? 8 : 28, 'm' }; }
    auto fg(color c)                { return ansi_code { 30 + c, 'm' }; }
    auto bg(color c)                { return ansi_code { 40 + c, 'm' }; }

    auto cursor_up(std::uint32_t p)     { return ansi_code { p, 'A' }; }
    auto cursor_down(std::uint32_t p)   { return ansi_code { p, 'B' }; }
    auto cursor_right(std::uint32_t p)  { return ansi_code { p, 'C' }; }
    auto cursor_left(std::uint32_t p)   { return ansi_code { p, 'D' }; }

    auto save_cursor_pos()      { return ansi_code { 's' }; }
    auto restore_cursor_pos()   { return ansi_code { 'u' }; }

    auto set_cursor(vector2i pos) { return ansi_code { pos[1] + 1, pos[0] + 1, 'H' }; }
    auto move_cursor(vector2i pos)
    {
        auto x = pos[0], y = pos[1];
        return (x < 0 ? cursor_left(std::abs(x)) : cursor_right(x))
             + (y < 0 ? cursor_up(std::abs(y))   : cursor_down(y));
    }

    auto clear_screen()                 { return ansi_code { 2, 'J' }; }
    auto clear_line()                   { return ansi_code { 'K' }; }
    auto insert_lines(std::uint32_t n)  { return ansi_code { n, 'L' }; }
    auto remove_lines(std::uint32_t n)  { return ansi_code { n, 'M' }; }
    auto insert_spaces(std::uint32_t n) { return ansi_code { n, '@' }; }
    auto erase_chars(std::uint32_t n)   { return ansi_code { n, 'P' }; }

    auto set_video_mode(std::uint32_t mode) { return ansi_code { '=', mode, 'h' }; }
    auto set_80x25_mode() { return set_video_mode(3); }
    auto set_80x50_mode() { return set_80x25_mode() + set_video_mode(43); }

    auto line_wrap(bool enable)     { return ansi_code { '?',  7, enable ? 'h' : 'l' }; }
    auto fast_scroll(bool enable)   { return ansi_code { '?', 98, enable ? 'h' : 'l' }; }
}
