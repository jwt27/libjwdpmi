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

            struct mpu401_streambuf : std::streambuf
            {
                mpu401_streambuf(mpu401_config c);
                virtual ~mpu401_streambuf();

                mpu401_streambuf(const mpu401_streambuf&) = delete;
                mpu401_streambuf(mpu401_streambuf&&) = delete;
                mpu401_streambuf& operator=(const mpu401_streambuf&) = delete;
                mpu401_streambuf& operator=(mpu401_streambuf&&) = delete;

            protected:
                virtual int sync() override;
                virtual std::streamsize xsgetn(char_type* s, std::streamsize n) override;
                virtual int_type underflow() override;
                virtual std::streamsize xsputn(const char_type* s, std::streamsize n) override;
                virtual int_type overflow(int_type c = traits_type::eof()) override;

            private:
                void get() noexcept
                {
                    if (getting.test_and_set()) return;
                    while (!status_port.read().no_data_available && rx_ptr < rx_buf.end()) 
                        *(rx_ptr++) = data_port.read();
                    setg(rx_buf.begin(), gptr(), rx_ptr);
                    getting.clear();
                }

                void put() noexcept
                {
                    if (putting.test_and_set()) return;
                    while (!status_port.read().dont_send_data && tx_ptr < pptr())
                        data_port.write(*tx_ptr++);
                    putting.clear();
                }

                dpmi::irq_handler irq_handler { [this]() INTERRUPT
                {
                    if (!status_port.read().no_data_available) dpmi::end_of_interrupt();
                    get();
                    put();
                } };

                mpu401_config cfg;
                out_port<byte> cmd_port;
                in_port<mpu401_status> status_port;
                io_port<byte> data_port;
                std::atomic_flag getting { false };
                std::atomic_flag putting { false };

                std::array<char_type, 1_KB> rx_buf;
                std::array<char_type, 1_KB> tx_buf;
                char_type* rx_ptr { rx_buf.data() };
                char_type* tx_ptr { tx_buf.data() };

                static std::unordered_map<port_num, bool> port_use_map;
            };
        }

        struct mpu401_stream : std::iostream
        {
            mpu401_stream(mpu401_config c) : std::iostream(&streambuf), streambuf(c) { }

        private:
            detail::mpu401_streambuf streambuf;
        };
    }
}
