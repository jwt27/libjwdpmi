#pragma once
#include <iostream>
#include <jw/io/detail/rs232.h>
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

        struct [[gnu::packed]] com_port_config
        {
            enum
            {
                char_5, 
                char_6, 
                char_7, 
                char_8
            } char_bits : 2;
            enum
            {
                stop_1, 
                stop_2
            } stop_bits : 1;
            enum
            {
                none = 0b000,
                odd = 0b001,
                even = 0b011,
                mark = 0b101,
                space = 0b111
            } parity : 3;
            unsigned : 1;
            bool divisor_access_latch : 1;
        };

        class rs232_stream : public std::iostream
        {
        public:
            rs232_stream(com_port p) : streambuf(p), std::iostream(&streambuf) { }

        private:
            detail::rs232_streambuf streambuf;
        };
    }
}