/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <iostream>
#include <mutex>
#include <unordered_set>
#include <jw/io/ioport.h>
#include <jw/dpmi/irq.h>
#include <jw/common.h>
#include <jw/io/io_error.h>
#include <jw/thread/mutex.h>
#include <jw/io/realtime_streambuf.h>

namespace jw
{
    namespace io
    {
        struct mpu401_config
        {
            port_num port { 0x330 };
            dpmi::irq_level irq { 9 };
            bool use_irq { true };
        };

        namespace detail
        {
            struct [[gnu::packed]] mpu401_status
            {
                unsigned : 6;
                bool dont_send_data : 1;
                bool no_data_available : 1;
            };

            struct mpu401_streambuf : realtime_streambuf
            {
                mpu401_streambuf(const mpu401_config& c);
                virtual ~mpu401_streambuf();

                mpu401_streambuf(const mpu401_streambuf&) = delete;
                mpu401_streambuf(mpu401_streambuf&&) = delete;
                mpu401_streambuf& operator=(const mpu401_streambuf&) = delete;
                mpu401_streambuf& operator=(mpu401_streambuf&&) = delete;

                virtual void put_realtime(char_type out) override;

            protected:
                virtual int sync() override;
                virtual std::streamsize xsgetn(char_type* s, std::streamsize n) override;
                virtual int_type underflow() override;
                virtual std::streamsize xsputn(const char_type* s, std::streamsize n) override;
                virtual int_type overflow(int_type c = traits_type::eof()) override;

            private:
                void check_irq_exception();

                mpu401_status get()
                {
                    const auto qsize = rx_buf.size() / 4;
                    const auto offset = gptr() - (rx_buf.begin() + qsize);
                    if (rx_ptr > rx_buf.begin() + qsize * 3 and offset > 0) [[unlikely]]
                    {
                        std::copy(rx_buf.begin() + offset, rx_ptr, rx_buf.begin());
                        rx_ptr -= offset;
                        setg(rx_buf.begin(), rx_buf.begin() + qsize, rx_ptr);
                    }

                    auto status = status_port.read();
                    while (not status.no_data_available)
                    {
                        if (rx_ptr == rx_buf.end()) throw io::overflow { "MPU401 receive buffer overflow" };
                        *rx_ptr++ = data_port.read();
                        status = status_port.read();
                    }
                    setg(rx_buf.begin(), gptr(), rx_ptr);
                    return status;
                }

                void do_sync()
                {
                    while (true)
                    {
                        auto status = get();
                        if (status.dont_send_data or tx_ptr == pptr()) break;
                        data_port.write(*tx_ptr++);
                    }
                }

                dpmi::irq_handler irq_handler { [this]()
                {
                    dpmi::interrupt_mask no_irq { };
                    try
                    {
                        if (not status_port.read().no_data_available)
                            dpmi::irq_handler::acknowledge();

                        do { do_sync(); } while (not status_port.read().no_data_available);
                    }
                    catch (...) { irq_exception = std::current_exception(); }
                } };

                mpu401_config cfg;
                out_port<byte> cmd_port;
                in_port<mpu401_status> status_port;
                io_port<byte> data_port;
                std::exception_ptr irq_exception;

                std::array<char_type, 1_KB> rx_buf;
                std::array<char_type, 1_KB> tx_buf;
                char_type* rx_ptr { rx_buf.data() };
                char_type* tx_ptr { tx_buf.data() };

                inline static std::unordered_set<port_num> ports_used { };
            };
        }

        struct mpu401_stream : std::iostream
        {
            mpu401_stream(mpu401_config c = { }) : std::iostream(nullptr), streambuf(new detail::mpu401_streambuf { c })
            {
                this->rdbuf(streambuf.get());
            }

            mpu401_stream(const mpu401_stream&) = delete;

        private:
            std::unique_ptr<detail::mpu401_streambuf> streambuf;
        };
    }
}
