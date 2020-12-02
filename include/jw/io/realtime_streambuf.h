/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <streambuf>

namespace jw::io
{
    template<typename CharT, typename Traits>
    struct basic_realtime_streambuf : std::basic_streambuf<CharT, Traits>
    {
        using std::basic_streambuf<CharT, Traits>::basic_streambuf;
        using std::basic_streambuf<CharT, Traits>::operator=;
        virtual ~basic_realtime_streambuf() = default;

        virtual void put_realtime(CharT) = 0;
    };

    using realtime_streambuf = basic_realtime_streambuf<char, std::char_traits<char>>;
}
