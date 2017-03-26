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
