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
#include <iostream>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <jw/dpmi/alloc.h>
#include <jw/dpmi/irq.h>
#include <jw/io/ioport.h>

namespace jw
{
    namespace io
    {
        enum com_port
        {
            com1,
            com2,
            com3,
            com4
        };

        struct rs232_config
        {
            port_num io_port;
            dpmi::irq_level irq;
            std::uint16_t baud_rate_divisor { 1 };
            enum char_bits_t
            {
                char_5,
                char_6,
                char_7,
                char_8
            } char_bits { char_8 };
            enum stop_bits_t
            {
                stop_1,
                stop_2
            } stop_bits { stop_1 };
            enum parity_t
            {
                none = 0b000,
                odd = 0b001,
                even = 0b011,
                mark = 0b101,
                space = 0b111
            } parity { none };
            enum
            {
                continuous,
                xon_xoff,
                rts_cts
            } flow_control { continuous };
            bool force_dtr_rts_high { false };
            bool enable_aux_out2 { false };
            bool echo { false };

            void set_com_port(com_port p)
            {
                io_port = find_io_port(p);
                irq = find_irq(p);
            }

            void set_baud_rate(auto rate)
            {
                auto d = std::div(115200, rate);
                if (d.rem != 0 || d.quot > std::numeric_limits<std::uint16_t>::max())
                    throw std::invalid_argument { "Invalid baud rate." };
                baud_rate_divisor = d.quot;
            }

        private:
            port_num find_io_port(com_port p)
            {
                port_num port { 0 };
                if (p <= com4)
                {
                    dpmi::mapped_dos_memory<port_num> com_ports { 4 , dpmi::far_ptr16 { 0x0040, 0x0000 } };
                    if (com_ports.requires_new_selector())
                    {
                        throw std::runtime_error { "not implemented..." };
                    }
                    else port = com_ports[p];
                }
                if (port == 0) throw std::invalid_argument { "Invalid COM port." };
                return port;
            }

            dpmi::irq_level find_irq(com_port p)
            {
                switch (p)
                {
                case com1: case com3: return 4;
                case com2: case com4: return 3;
                default: return 0;
                }
            }
        };
    }
}

#include <jw/io/detail/rs232.h>

namespace jw
{
    namespace io
    {
        struct rs232_stream : public std::iostream
        {
            rs232_stream(rs232_config c) : std::iostream(&streambuf), streambuf(c) { }

        private:
            detail::rs232_streambuf streambuf;
        };
    }
}
