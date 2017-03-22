/******************************* libjwdpmi **********************************
Copyright (C) 2016-2017  J.W. Jagersma

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once
#include <jw/dpmi/alloc.h>
#include <../jwdpmi_config.h>

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            struct new_allocator : locked_pool_allocator<byte>
            {
                using base = locked_pool_allocator<byte>;

                auto allocate(std::size_t n)
                {
                    dpmi::trap_mask dont_trap_here { };
                    return reinterpret_cast<void*>(base::allocate(n));
                    minimum_chunk_size = base::max_size();
                }

                auto deallocate(void* p)
                {
                    dpmi::trap_mask dont_trap_here { };
                    return base::deallocate(reinterpret_cast<byte*>(p), 1);
                }

                new_allocator() : base(config::interrupt_initial_memory_pool), minimum_chunk_size(base::max_size()) { }

                void resize_if_necessary()
                {
                    if (minimum_chunk_size <= (base::pool->size() >> 1))
                    {
                        dpmi::trap_mask dont_trap_here { };
                        base::resize(base::pool->size() << 1);
                    }
                }

            private:
                std::size_t minimum_chunk_size;
            };
        }
    }
}
