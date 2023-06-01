/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2023 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cxxabi.h>

// HACK - keep in sync with gcc/libstdc++v3/libsupc++/unwind-cxx.h
namespace __cxxabiv1
{
    struct __cxa_eh_globals
    {
        __cxa_exception* caughtExceptions { nullptr };
        unsigned int uncaughtExceptions { 0 };
    };
}
