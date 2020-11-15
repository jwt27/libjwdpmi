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
                mpu401_streambuf(const mpu401_config& c);
                virtual ~mpu401_streambuf();

                mpu401_streambuf(const mpu401_streambuf&) = delete;
                mpu401_streambuf(mpu401_streambuf&&) = delete;
                mpu401_streambuf& operator=(const mpu401_streambuf&) = delete;
                mpu401_streambuf& operator=(mpu401_streambuf&&) = delete;

                void put_realtime(char_type out)
                {
                    while (true)
                    {
                        {
                            dpmi::interrupt_mask no_irq { };
                            if (not status_port.read().dont_send_data)
                            {
                                data_port.write(out);
                                return;
                            }
                        }
                        thread::yield();
                    }
                }

            protected:
                virtual int sync() override;
                virtual std::streamsize xsgetn(char_type* s, std::streamsize n) override;
                virtual int_type underflow() override;
                virtual std::streamsize xsputn(const char_type* s, std::streamsize n) override;
                virtual int_type overflow(int_type c = traits_type::eof()) override;

            private:
                void check_irq_exception()
                {
                    if (irq_exception != nullptr) [[unlikely]]
                    {
                        auto e = std::move(irq_exception);
                        irq_exception = nullptr;
                        std::rethrow_exception(e);
                    }
                }

                void get()
                {
                    dpmi::interrupt_mask no_irq { };
                    do
                    {
                        if (rx_ptr >= rx_buf.end()) throw io::overflow { "MPU401 receive buffer overflow" };
                        *(rx_ptr++) = data_port.read();
                    } while (not status_port.read().no_data_available);
                    setg(rx_buf.begin(), gptr(), rx_ptr);
                }

                void put()
                {
                    std::unique_lock<thread::recursive_mutex> locked { putting, std::try_to_lock };
                    if (not locked) return;
                    dpmi::interrupt_mask no_irq { };
                    while (tx_ptr < pptr() and not status_port.read().dont_send_data)
                        data_port.write(*tx_ptr++);
                }

                dpmi::irq_handler irq_handler { [this]()
                {
                    try
                    {
                        if (not status_port.read().no_data_available)
                        {
                            dpmi::irq_handler::acknowledge();
                            get();
                        }
                        put();
                    }
                    catch (...) { irq_exception = std::current_exception(); }
                } };

                mpu401_config cfg;
                out_port<byte> cmd_port;
                in_port<mpu401_status> status_port;
                io_port<byte> data_port;
                thread::recursive_mutex getting, putting;
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
