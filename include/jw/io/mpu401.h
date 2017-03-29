/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <iostream>
#include <jw/io/ioport.h>
#include <jw/dpmi/irq.h>
#include <jw/common.h>

namespace jw
{
    namespace io
    {
        struct mpu401_config
        {
            port_num port { 0x330 };
            dpmi::irq_level irq { 9 };
            bool use_irq { false };
        };

        namespace detail
        {
            struct [[gnu::packed]] mpu401_status
            {
                unsigned : 6;
                bool dont_send_data : 1;
                bool no_data_available : 1;
            };

            struct mpu401_streambuf : std::basic_streambuf<byte, std::char_traits<byte>>
            {
                mpu401_streambuf(mpu401_config c);
                virtual ~mpu401_streambuf();

            protected:
                virtual int sync() override;
                virtual std::streamsize xsgetn(char_type* s, std::streamsize n) override;
                virtual int_type underflow() override;
                virtual std::streamsize xsputn(const char_type* s, std::streamsize n) override;
                virtual int_type overflow(int_type c = traits_type::eof()) override;

            private:
                mpu401_config cfg;
                out_port<byte> cmd_port;
                in_port<mpu401_status> status_port;
                io_port<byte> data_port;

                static std::unordered_map<port_num, bool> port_use_map;
            };
        }

        struct mpu401_stream : std::basic_iostream<byte, std::char_traits<byte>>
        {
            mpu401_stream(mpu401_config c) : std::basic_iostream<byte, std::char_traits<byte>>(&streambuf), streambuf(c) { }

        private:
            detail::mpu401_streambuf streambuf;
        };
    }
}
