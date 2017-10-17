/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <jw/io/detail/keyboard_streambuf.h>

namespace jw
{
    namespace io
    {
        namespace detail
        {
            int keyboard_streambuf::sync()
            {
                if (egptr() >= buffer.end() && gptr() == buffer.begin()) gbump(1);
                std::copy(gptr(), ptr, buffer.begin());
                ptr = buffer.begin() + (ptr - gptr());
                setg(buffer.begin(), buffer.begin(), ptr);
                thread::yield();
                return 0;
            }

            std::streamsize keyboard_streambuf::xsgetn(char_type * s, std::streamsize n)
            {
                std::streamsize max_n = 0;
                while (max_n < n && underflow() != traits_type::eof()) max_n = std::min(egptr() - gptr(), n);
                std::copy_n(gptr(), max_n, s);
                setg(buffer.begin(), gptr() + max_n, egptr());
                return max_n;
            }

            keyboard_streambuf::int_type keyboard_streambuf::underflow()
            {
                thread::yield_while([this] { return gptr() == egptr(); });
                return *gptr();
            }
        }
    }
}
