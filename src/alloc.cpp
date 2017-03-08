#include <jw/dpmi/alloc.h>

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            std::unique_ptr<std::map<void*, data_lock>> locking_allocator_base::map;
        }
    }
}
