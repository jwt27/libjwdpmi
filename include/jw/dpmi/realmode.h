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
#include <jw/dpmi/memory.h>

namespace jw
{
    namespace dpmi
    {
        // CPU register structure for DPMI real-mode functions.
        struct alignas(2) [[gnu::packed]] rm_registers : public cpu_registers
        {
            union[[gnu::packed]]
            {
                std::uint16_t raw_flags;
                struct[[gnu::packed]]
                {
                    bool carry : 1;
                    unsigned : 1;
                    bool parity : 1;
                    unsigned : 1;
                    bool adjust : 1;
                    unsigned : 1;
                    bool zero : 1;
                    bool sign : 1;
                    bool trap : 1;
                    bool interrupt : 1;
                    bool direction : 1;
                    bool overflow : 1;
                    unsigned iopl : 2;
                    bool nested_task : 1;
                    unsigned : 1;
                } flags;
            };
            std::uint16_t es, ds, fs, gs;
            std::uint16_t ip, cs; // not used in call_rm_interrupt()
            std::uint16_t sp, ss; // not required for call_rm_interrupt()

            auto& print(std::ostream& out) const
            {
                using namespace std;
                out << hex << setfill('0');
                out << "es=" << setw(4) << es << " ds=" << setw(4) << ds << " fs=" << setw(4) << fs << " gs=" << setw(4) << gs << "\n";
                out << "cs=" << setw(4) << cs << " ip=" << setw(4) << ip << " ss=" << setw(4) << ss << " sp=" << setw(4) << sp << " flags=" << setw(4) << flags_reg << "\n";
                out << hex << setfill(' ') << setw(0) << flush;
                return out;
            }
            friend auto& operator<<(std::ostream& out, const rm_registers& in) { return in.print(out); }
        };

        static_assert(sizeof( rm_registers) == 0x32, "check sizeof struct dpmi::rm_registers");

        inline void call_rm_interrupt(std::uint8_t interrupt, rm_registers* reg)
        {
            selector new_reg_ds = get_ds();
            rm_registers* new_reg;
            dpmi_error_code error;
            bool c;

            asm volatile(
                "mov es, %w2;"
                "int 0x31;"
                "mov %w2, es;"
                : "=@ccc" (c)
                , "=a" (error)
                , "+rm" (new_reg_ds)
                , "=D" (new_reg)
                : "a" (0x0300)
                , "b" (interrupt)
                , "D" (reg)
                , "c" (0)   // TODO: stack?
                : "memory");
            if (c) throw dpmi_error(error, "call_rm_interrupt");

            if (new_reg != reg || new_reg_ds != get_ds())   // copy back if location changed.
                *reg = *(memory(new_reg_ds, new_reg).get_ptr<rm_registers>());
        }
    }
}
