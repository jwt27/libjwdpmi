/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
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
                this_thread::yield();
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
                this_thread::yield_while([this] { return gptr() == egptr(); });
                return *gptr();
            }

            bool keyboard_streambuf::event_handler(key k, key_state s)
            {
                auto insert = [this](char c)
                {
                    *(ptr++) = c;
                    if (echo)
                    {
                        *echo_stream << c;
                        if (c == '\b')* echo_stream << " \b";
                        *echo_stream << std::flush;
                    }
                    setg(buffer.begin(), gptr(), ptr);
                };

                if (egptr() >= buffer.end()) sync();

                if (k == key::any_alt and s == key_state::up)
                {
                    if (alt_sequence <= 0xff) insert(alt_sequence);
                    alt_sequence = 0x80000000;
                    return true;
                }

                if (s.is_up()) return false;

                if (keyb[key::any_alt])
                {
                    if (not (keyb[key::any_shift] xor keyb[key::num_lock_state])) return false;
                    if (keyb[key::any_ctrl]) return false;
                    auto n = [k]
                    {
                        switch (k)
                        {
                        case key::num_0: return 0;
                        case key::num_1: return 1;
                        case key::num_2: return 2;
                        case key::num_3: return 3;
                        case key::num_4: return 4;
                        case key::num_5: return 5;
                        case key::num_6: return 6;
                        case key::num_7: return 7;
                        case key::num_8: return 8;
                        case key::num_9: return 9;
                        default: return -1;
                        }
                    }();
                    if (n == -1) return false;
                    alt_sequence *= 10;
                    alt_sequence += n;
                    return true;
                }

                if (auto c = k.to_ascii(keyb))
                {
                    insert(c);
                    return true;
                }
                return false;
            }
        }
    }
}
