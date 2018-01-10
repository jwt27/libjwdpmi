/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#include <jw/dpmi/irq_mask.h>

using namespace std;
namespace jw
{
    namespace dpmi
    {
        std::array<irq_mask::mask_counter, 16> irq_mask::map { };
    }
}
