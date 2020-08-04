/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/dpmi/alloc.h>
#include <../jwdpmi_config.h>

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            struct new_allocator : locked_pool_allocator<false, byte>, class_lock<new_allocator>
            {
                using base = locked_pool_allocator<false, byte>;

                auto allocate(std::size_t n)
                {
                    debug::trap_mask dont_trap_here { };
                    auto* p = reinterpret_cast<void*>(base::allocate(n));
                    minimum_chunk_size = base::max_size();
                    return p;
                }

                auto deallocate(void* p)
                {
                    debug::trap_mask dont_trap_here { };
                    return base::deallocate(reinterpret_cast<byte*>(p), 1);
                }

                new_allocator() : base(config::interrupt_initial_memory_pool), minimum_chunk_size(base::max_size()) { }

                void resize_if_necessary()
                {
                    if (__builtin_expect(minimum_chunk_size <= (base::pool->size() >> 1), false))
                    {
                        debug::trap_mask dont_trap_here { };
                        base::resize(base::pool->size() << 1);
                    }
                }

            private:
                std::size_t minimum_chunk_size;
            };
        }
    }
}
