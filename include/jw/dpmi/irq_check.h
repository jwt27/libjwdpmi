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
#include <cstdint>
#include <stdexcept>

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            extern volatile std::uint32_t interrupt_count;
            extern volatile std::uint32_t exception_count;
        }

        struct bad_irq_function_call : public std::runtime_error
        {
            bad_irq_function_call() : std::runtime_error("Illegal function call from interrupt routine.") { }
        };
        
        // Returns true if currently in irq or exception context.
        inline bool in_irq_context() noexcept { return detail::interrupt_count > 0 || detail::exception_count > 0; }

        // Throws bad_irq_function_call if currently in irq or exception context.
        inline void throw_if_irq() { if (in_irq_context()) throw bad_irq_function_call { }; };
    }
}
