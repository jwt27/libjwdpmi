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

int main(int argc, char** argv)
{
    _crt0_startup_flags &= ~_CRT0_FLAG_LOCK_MEMORY;
    try { throw 0; } catch(...) { }     // Looks silly, but this speeds up subsequent exceptions.
    
    std::deque<std::string> args { };   // TODO: std::string_view when it's available
    for (auto i = 0; i < argc; ++i)
        args.emplace_back(argv[i]);
    
    try 
    {   
        return jwdpmi_main(args); 
    }
    catch (const std::exception& e) { throw; /* TODO */ }
    catch (...) { throw; /* TODO */ }
}
