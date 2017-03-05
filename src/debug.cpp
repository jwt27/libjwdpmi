#include <jw/dpmi/cpu_exception.h>

extern "C" void breakpoint();
extern "C" void putDebugChar(int c) { }
extern "C" int getDebugChar() { return 0; }
extern "C" void exceptionHandler(int exception, void* handler);
    //{ jw::dpmi::cpu_exception::set_handler(exception, reinterpret_cast<void(*)()>(handler)); }

namespace jw
{
    namespace dpmi
    {
        void breakpoint() noexcept { ::breakpoint(); }
    } 
}