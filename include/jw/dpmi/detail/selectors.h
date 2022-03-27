/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/dpmi/dpmi.h>

namespace jw::dpmi::detail
{
    inline selector main_cs;
    inline selector main_ds;

    inline selector ring0_cs;
    inline selector ring0_ss;

    inline selector safe_ds;
}
