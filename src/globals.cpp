/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2024 - 2024 J.W. Jagersma, see COPYING.txt for details    */

#include <jw/dpmi/bda.h>
#include <jw/dpmi/detail/selectors.h>
#include <jw/dpmi/fpu.h>

namespace jw::dpmi
{
    bios_data_area* const bda { };
}

namespace jw::dpmi::detail
{
    const selector main_cs { };
    const selector main_ds { };
    const selector safe_ds { };

    const bool use_fxsave { false };
}
