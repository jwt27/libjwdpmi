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

#include <jw/dpmi/cpu_exception.h>

//extern "C" void breakpoint();
//extern "C" void putDebugChar(int) { }
//extern "C" int getDebugChar() { return 0; }
//extern "C" void exceptionHandler(int exception, void* handler);
    //{ jw::dpmi::cpu_exception::set_handler(exception, reinterpret_cast<void(*)()>(handler)); }

namespace jw
{
    namespace dpmi
    {
        void breakpoint() noexcept { asm("int 3"); } //{ ::breakpoint(); }
    } 
}