/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/alloc.h>
#include <jw/dpmi/alloc.h>

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            void setup_gdb_interface(std::unique_ptr<std::iostream, allocator_delete<jw::dpmi::locking_allocator<std::iostream>>>&&);
        }
    }
}
