/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
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

                if (thread::yield_while_for<chrono::pit>([this] { return status_port.read().dont_send_data; }, timeout))
                    throw std::runtime_error("Timeout while waiting for MPU401.");

                while (!status_port.read().no_data_available) data_port.read();

                cmd_port.write(0xFF);   // reset (this won't ACK if the MPU is in uart mode already)
                if (!thread::yield_while_for<chrono::pit>([this] { return status_port.read().no_data_available; }, timeout))
                    if (data_port.read() != 0xFE) throw std::runtime_error("Expected ACK from MPU401.");

                if (thread::yield_while_for<chrono::pit>([this] { return status_port.read().dont_send_data; }, timeout))
                    throw std::runtime_error("Timeout while waiting for MPU401.");

                cmd_port.write(0x3F);   // set UART mode (should ACK, maybe some cheap cards won't)
                if (!thread::yield_while_for<chrono::pit>([this] { return status_port.read().no_data_available; }, timeout))
                    if (data_port.read() != 0xFE) throw std::runtime_error("Expected ACK from MPU401.");
            }

            mpu401_streambuf::~mpu401_streambuf()
            {
                cmd_port.write(0xFF);
                port_use_map[cfg.port] = false;
            }

            int mpu401_streambuf::sync()
            {
                return 0;
            }

            std::streamsize mpu401_streambuf::xsgetn(char_type * s, std::streamsize n)
            {
                return std::streamsize();
            }

            mpu401_streambuf::int_type mpu401_streambuf::underflow()
            {
                return int_type();
            }

            std::streamsize mpu401_streambuf::xsputn(const char_type * s, std::streamsize n)
            {
                return std::streamsize();
            }

            mpu401_streambuf::int_type mpu401_streambuf::overflow(int_type c)
            {
                return int_type();
            }
        }
    }
}
