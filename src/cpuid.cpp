/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */

#include <jw/dpmi/cpuid.h>

namespace jw::dpmi
{
    void cpuid::populate()
    {
        bool have_cpuid;
        std::uint32_t scratch;
        asm
        (
            "pushfd;"
            "mov %0, [esp];"
            "xor dword ptr [esp], 0x00200000;"    // ID bit
            "popfd;"
            "pushfd;"
            "cmp %0, [esp];"
            "pop %0;"
            : "=&r" (scratch)
            , "=@ccne" (have_cpuid)
        );
        if (not have_cpuid) return;

        std::uint32_t max = 0;
        for (std::uint32_t i = 0; i <= max; ++i)
        {
            auto& l = leaves[i];
            asm("cpuid" : "=a" (l.eax), "=b" (l.ebx), "=c" (l.ecx), "=d" (l.edx) : "a" (i));
            if (i == 0) max = l.eax;
        }
    }
}
