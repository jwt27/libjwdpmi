#include <string>
#include <deque>
#include <crt0.h>
#include <jw/dpmi/debug.h>
#include <../jwdpmi_config.h>

int _crt0_startup_flags = 0
| jw::config::user_crt0_startup_flags
| _CRT0_FLAG_NMI_SIGNAL
| _CRT0_DISABLE_SBRK_ADDRESS_WRAP
| _CRT0_FLAG_NONMOVE_SBRK
| _CRT0_FLAG_LOCK_MEMORY;
    //| _CRT0_FLAG_NEARPTR;
    //| _CRT0_FLAG_NULLOK
    //| _CRT0_FLAG_FILL_DEADBEEF;

int jwdpmi_main(std::deque<std::string>);

extern "C" void set_debug_traps();

int main(int argc, char** argv)
{
    _crt0_startup_flags &= ~_CRT0_FLAG_LOCK_MEMORY;
    try { throw 0; } catch(...) { }     // Looks silly, but this speeds up subsequent exceptions.
    
    //set_debug_traps();
    
    std::deque<std::string> args { };   // TODO: std::string_view when it's available
    for (auto i = 0; i < argc; ++i)
        args.emplace_back(argv[i]);
    
    try 
    {   
        //jw::dpmi::breakpoint();
        return jwdpmi_main(args); 
    }
    catch(const std::exception& e) { /* TODO */ }
    catch(...) { /* TODO */ }
}
