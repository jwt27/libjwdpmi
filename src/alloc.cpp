/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#include <jw/dpmi/alloc.h>

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            std::map<void*, data_lock>* locking_allocator_base::map { nullptr };
        }

        std::map<void*, locking_memory_resource::ptr_with_lock>* locking_memory_resource::map;
    }
}
