#include <jw/dpmi/cpu_exception.h>

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            volatile std::uint32_t exception_count { 0 };
        }
        std::array<std::deque<exception_handler*>, 0x20> exception_handler::wrapper_list;
        std::array<std::array<byte, config::exception_stack_size>, 0x20> exception_handler::stacks;
    }
}