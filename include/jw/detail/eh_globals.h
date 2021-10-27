/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cxxabi.h>

namespace jw::detail
{
    struct jw_cxa_eh_globals
    {
        void* caughtExceptions { nullptr };
        unsigned int uncaughtExceptions { 0 };
    };

    static void set_eh_globals(const jw_cxa_eh_globals& g) { *reinterpret_cast<jw_cxa_eh_globals*>(abi::__cxa_get_globals()) = g; }
    static jw_cxa_eh_globals get_eh_globals() { return *reinterpret_cast<jw_cxa_eh_globals*>(abi::__cxa_get_globals()); }
}
