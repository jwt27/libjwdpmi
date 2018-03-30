/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
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
}
