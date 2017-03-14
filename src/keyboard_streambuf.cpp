/******************************* libjwdpmi **********************************
Copyright (C) 2016-2017  J.W. Jagersma

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

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
