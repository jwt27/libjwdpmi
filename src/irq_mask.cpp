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

#include <jw/dpmi/irq_mask.h>

using namespace std;
namespace jw
{
    namespace dpmi
    {
        volatile int interrupt_mask::count { 0 };
        bool interrupt_mask::initial_state;

        std::array<irq_mask::mask_counter, 16> irq_mask::map { };
        constexpr io::io_port<byte> irq_mask::pic0_data;
        constexpr io::io_port<byte> irq_mask::pic1_data;

        //int nmi_mask::count { 0 };
    }
}
