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

                auto timeout = std::chrono::milliseconds { 100 };

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
                while (tx_ptr < pptr())
                {
                    overflow();
                    underflow();
                    thread::yield();
                }
                return 0;
            }

            std::streamsize mpu401_streambuf::xsgetn(char_type * s, std::streamsize n)
            {
                std::streamsize max_n = 0;
                while (max_n < n && underflow() != traits_type::eof()) max_n = std::min(rx_ptr - gptr(), n);
                std::copy_n(gptr(), max_n, s);
                setg(rx_buf.begin(), gptr() + max_n, rx_ptr);
                return max_n;
            }

            mpu401_streambuf::int_type mpu401_streambuf::underflow()
            {
                if (gptr() != rx_buf.begin()) std::copy(gptr(), rx_ptr, rx_buf.begin());
                rx_ptr = rx_buf.begin() + (rx_ptr - gptr()); 
                setg(rx_buf.begin(), rx_buf.begin(), rx_ptr);
                do 
                { 
                    get(); 
                    thread::yield();
                } while (rx_ptr == gptr());
                return *gptr();
            }

            std::streamsize mpu401_streambuf::xsputn(const char_type * s, std::streamsize n)
            {
                auto max_n = std::min(tx_buf.end() - pptr(), n);
                if (max_n < n) overflow();
                max_n = std::min(tx_buf.end() - pptr(), n);
                std::copy_n(s, max_n, pptr());
                setp(pptr() + max_n, tx_buf.end());
                return max_n;
            }

            mpu401_streambuf::int_type mpu401_streambuf::overflow(int_type c)
            {
                do
                {
                    put();
                    if (tx_ptr != tx_buf.begin()) std::copy(tx_ptr, pptr(), tx_buf.begin());
                    setp(tx_buf.begin() + (pptr() - tx_ptr), tx_buf.end());
                    tx_ptr = tx_buf.begin();
                    thread::yield();
                } while (pptr() == epptr());
                if (traits_type::not_eof(c)) sputc(c);
                return ~traits_type::eof();
            }
        }
    }
}
