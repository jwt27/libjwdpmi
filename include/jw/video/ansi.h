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
    constexpr char esc = 27;

    bool install_check()
    {
        dpmi::realmode_registers r { };
        r.ax = 0x1a00;
        r.call_int(0x2f);
        return r.al == 0xff;
    }

    enum color : std::uint8_t { black, red, green, yellow, blue, magenta, cyan, white };

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
            c.emit(std::make_index_sequence<std::tuple_size_v<decltype(string)>> { }, out);
            out.flags(f);
            return out;
        }

        template<typename... U>
        friend constexpr auto operator+(const ansi_code& lhs, const ansi_code<U...>& rhs)
        {
            return ansi_code<T..., char, char, U...> { std::tuple_cat(lhs.string, rhs.string) };
        }

    private:
        template<std::size_t i>
        constexpr static bool is_char()
        {
            return std::is_same_v<std::tuple_element_t<i, decltype(string)>, char>;
        };

        template<std::size_t... is>
        void emit(std::index_sequence<is...>, std::ostream& out) const { emit<is...>(out); }

        template<std::size_t i, std::size_t... next>
        void emit(std::ostream& out) const
        {
            auto j = std::get<i>(string);
            if constexpr (not is_char<i>())
            {
                if (j != 0) out << j;
                if constexpr (not is_char<i + 1>()) out << ';';
            }
            else out.put(j);
            if constexpr (sizeof...(next) > 0) emit<next...>(out);
        }
    };

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

    auto set_cursor(vector2i pos)       { return ansi_code { pos[1] + 1, pos[0] + 1, 'H' }; }
    auto move_cursor(vector2i pos)
    {
        auto x = pos[0], y = pos[1];
        return (x < 0 ? cursor_left(std::abs(x)) : cursor_right(x))
             + (y < 0 ? cursor_up(std::abs(y))   : cursor_down(y));
    }

    auto clear_screen() { return ansi_code { 2, 'J' }; }
}
