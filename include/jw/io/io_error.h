/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <stdexcept>

namespace jw::io
{
    struct io_error : public std::runtime_error { using runtime_error::runtime_error; };
    struct overflow : public io_error { using io_error::io_error; };
    struct parity_error : public io_error { using io_error::io_error; };
    struct framing_error : public io_error { using io_error::io_error; };
    struct line_break : public io_error { using io_error::io_error; };
    struct timeout_error : public io_error { using io_error::io_error; };

    struct device_not_found : public std::runtime_error { using runtime_error::runtime_error; };

    struct failure : std::ios::failure
    {
        explicit failure(const char* msg) : std::ios::failure { msg } { }
    };

    struct end_of_file : std::ios::failure
    {
        explicit end_of_file() : std::ios::failure { "end of file" } { }
    };
}
