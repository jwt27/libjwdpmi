/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <jw/io/mpu401.h>
#include <jw/chrono/chrono.h>

namespace jw
{
    namespace io
    {
        namespace detail
        {
            std::unordered_map<port_num, bool> mpu401_streambuf::port_use_map { };

            mpu401_streambuf::mpu401_streambuf(mpu401_config c) : cfg(c), cmd_port(cfg.port + 1), status_port(cfg.port + 1), data_port(cfg.port)
            {
                if (port_use_map[cfg.port]) throw std::runtime_error("MPU401 port already in use.");

                auto timeout = std::chrono::milliseconds { 10 };

                if (thread::yield_while_for([this] { return status_port.read().dont_send_data; }, timeout))
                    throw std::runtime_error("Timeout while waiting for MPU401.");

                while (!status_port.read().no_data_available) data_port.read();

                cmd_port.write(0xFF);   // reset (this won't ACK if the MPU is in uart mode already)
                if (!thread::yield_while_for([this] { return status_port.read().no_data_available; }, timeout))
                    if (data_port.read() != 0xFE) throw std::runtime_error("Expected ACK from MPU401.");

                if (thread::yield_while_for([this] { return status_port.read().dont_send_data; }, timeout))
                    throw std::runtime_error("Timeout while waiting for MPU401.");

                cmd_port.write(0x3F);   // set UART mode (should ACK, maybe some cheap cards won't)
                if (!thread::yield_while_for([this] { return status_port.read().no_data_available; }, timeout))
                    if (data_port.read() != 0xFE) throw std::runtime_error("Expected ACK from MPU401.");

                setg(rx_buf.begin(), rx_buf.begin(), rx_buf.begin());
                setp(tx_buf.begin(), tx_buf.end());

                irq_handler.set_irq(cfg.irq);
                if (cfg.use_irq) irq_handler.enable();
            }

            mpu401_streambuf::~mpu401_streambuf()
            {
                irq_handler.disable();
                cmd_port.write(0xFF);
                port_use_map[cfg.port] = false;
            }

            int mpu401_streambuf::sync()
            {
                thread::yield_while([this]
                {
                    {
                        std::unique_lock<std::recursive_mutex> lock { getting };
                        if (not status_port.read().no_data_available) underflow();
                    }
                    overflow();
                    return tx_ptr < pptr();
                });
                return 0;
            }

            std::streamsize mpu401_streambuf::xsgetn(char_type * s, std::streamsize n)
            {
                std::unique_lock<std::recursive_mutex> lock { getting };
                std::streamsize max_n;
                thread::yield_while([&]
                {
                    max_n = std::min(egptr() - gptr(), n);
                    return max_n < n and underflow() != traits_type::eof();
                });
                {
                    dpmi::interrupt_mask no_irq { };
                    std::copy_n(gptr(), max_n, s);
                    setg(rx_buf.begin(), gptr() + max_n, egptr());
                }
                return max_n;
            }

            mpu401_streambuf::int_type mpu401_streambuf::underflow()
            {
                std::unique_lock<std::recursive_mutex> lock { getting };
                auto qsize = rx_buf.size() / 4;
                if (rx_ptr > rx_buf.begin() + qsize * 3)
                {
                    dpmi::interrupt_mask no_irq { };
                    auto offset = gptr() - (rx_buf.begin() + qsize);
                    std::copy(rx_buf.begin() + offset, rx_ptr, rx_buf.begin());
                    rx_ptr -= offset;
                    setg(rx_buf.begin(), rx_buf.begin() + qsize, rx_ptr);
                }
                do
                {
                    check_irq_exception();
                    if (not cfg.use_irq or
                        not dpmi::interrupt_mask::enabled() or
                        not dpmi::irq_mask::enabled(cfg.irq))
                        get();
                    else thread::yield();
                } while (gptr() == rx_ptr);
                return *gptr();
            }

            std::streamsize mpu401_streambuf::xsputn(const char_type * s, std::streamsize n)
            {
                std::unique_lock<std::recursive_mutex> lock { putting };
                std::streamsize max_n;
                while ((max_n = std::min(tx_buf.end() - pptr(), n)) < n)
                    overflow();
                {
                    dpmi::interrupt_mask no_irq { };
                    std::copy_n(s, max_n, pptr());
                    setp(pptr() + max_n, tx_buf.end());
                }
                return max_n;
            }

            mpu401_streambuf::int_type mpu401_streambuf::overflow(int_type c)
            {
                std::unique_lock<std::recursive_mutex> lock { putting };
                thread::yield_while([this]
                {
                    check_irq_exception();
                    {
                        dpmi::interrupt_mask no_irq { };
                        put();
                    }
                    return tx_ptr < pptr();
                });

                auto hsize = tx_buf.size() / 2;
                if (pptr() > tx_buf.begin() + hsize)
                {
                    dpmi::interrupt_mask no_irq { };
                    std::copy(tx_ptr, pptr(), tx_buf.begin());
                    setp(tx_buf.begin() + (pptr() - tx_ptr), tx_buf.end());
                    tx_ptr = tx_buf.begin();
                }

                if (traits_type::not_eof(c)) sputc(c);
                return ~traits_type::eof();
            }
        }
    }
}
