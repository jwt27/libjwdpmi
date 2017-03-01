#include <jw/dpmi/alloc.h>

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            std::unique_ptr<std::unordered_map<void*, data_lock>> locking_allocator_base::map;
        }
    }
}
