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
                switch (p)  // TODO: look up port numbers from 0040:0000
                {
                case com1: return 0x3f8;
                case com2: return 0x2f8;
                case com3: return 0x3e8;
                case com4: return 0x2e8;
                default: throw std::invalid_argument { "Unknown COM port." };
                }
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