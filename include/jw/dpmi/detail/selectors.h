/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/dpmi/dpmi.h>

namespace jw::dpmi::detail
{
    extern const selector safe_ds;

    extern const selector main_cs;
    extern const selector main_ds;

    inline selector ring0_cs;
    inline selector ring0_ss;

    void setup_direct_ldt_access() noexcept;
}
