/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2023 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <jw/io/mpu401.h>
#include <jw/chrono.h>
#include <jw/thread.h>

namespace jw::io::detail
{
    mpu401_streambuf::mpu401_streambuf(const mpu401_config& c) : cfg(c), cmd_port(cfg.port + 1), status_port(cfg.port + 1), data_port(cfg.port)
    {
        if (ports_used.contains(cfg.port)) throw std::runtime_error("MPU401 port already in use.");
        ports_used.insert(cfg.port);

        try
        {
            using namespace std::chrono_literals;

            auto fail = [] { throw device_not_found { "MPU401 not detected" }; };

            std::optional<dpmi::irq_mask> no_irq;
            if (cfg.use_irq) no_irq.emplace(cfg.irq);

            auto timeout = this_thread::yield_while_for([this]
            {
                while (not status_port.read().no_data_available) data_port.read();
                return status_port.read().dont_send_data;
            }, 100ms);
            if (timeout) fail();

            cmd_port.write(0xff);   // Reset (this won't ACK if the MPU is in uart mode already)

            timeout = this_thread::yield_while_for([this, fail]
            {
                const auto s = status_port.read();
                if (not s.no_data_available and data_port.read() != 0xfe) fail();
                return s.dont_send_data;
            }, 100ms);
            if (timeout) fail();

            cmd_port.write(0x3f);   // Set UART mode (should ACK, maybe some cheap cards won't)

            timeout = this_thread::yield_while_for([this] { return status_port.read().no_data_available; }, 250ms);
            if (not timeout)
            {
                do
                {
                    if (data_port.read() != 0xfe) fail();
                } while (not status_port.read().no_data_available);
            }

            setg(rx_buf.begin(), rx_buf.begin(), rx_buf.begin());
            setp(tx_buf.begin(), tx_buf.end());

            irq_handler.set_irq(cfg.irq);
            if (cfg.use_irq) irq_handler.enable();
        }
        catch (...)
        {
            ports_used.erase(cfg.port);
            throw;
        }
    }

    mpu401_streambuf::~mpu401_streambuf()
    {
        irq_handler.disable();
        this_thread::yield_while([this]
        {
            auto status = status_port.read();
            while (not status.no_data_available)
            {
                data_port.read();
                status = status_port.read();
            }
            return status.dont_send_data;
        });
        cmd_port.write(0xff);
        ports_used.erase(cfg.port);
    }

    void mpu401_streambuf::put_realtime(char_type out)
    {
        while (true)
        {
            {
                std::optional<dpmi::interrupt_mask> no_irq;
                if (cfg.use_irq) no_irq.emplace();
                auto status = get();
                if (not status.dont_send_data)
                {
                    data_port.write(out);
                    return;
                }
            }
            this_thread::yield();
        }
    }

    int mpu401_streambuf::sync()
    {
        if (cfg.use_irq)
        {
            this_thread::yield_while([this]
            {
                check_irq_exception();
                {
                    dpmi::interrupt_mask no_irq { };
                    do_sync();
                }
                return tx_ptr < pptr();
            });
        }
        else
        {
            this_thread::yield_while([this]
            {
                do_sync();
                return tx_ptr < pptr();
            });
        }
        setp(tx_buf.begin(), tx_buf.end());
        tx_ptr = tx_buf.begin();
        return 0;
    }

    std::streamsize mpu401_streambuf::xsgetn(char_type* s, std::streamsize max)
    {
        std::streamsize size = 0;
        while (size < max)
        {
            auto n = std::min(egptr() - gptr(), max - size);
            std::copy_n(gptr(), n, s);
            gbump(n);
            s += n;
            size += n;
            if (size < max) underflow();
        }
        return size;
    }

    mpu401_streambuf::int_type mpu401_streambuf::underflow()
    {
        this_thread::yield_while([this]
        {
            check_irq_exception();
            {
                std::optional<dpmi::interrupt_mask> no_irq;
                if (cfg.use_irq) no_irq.emplace();
                get();
            }
            return gptr() == rx_ptr;
        });
        return *gptr();
    }

    std::streamsize mpu401_streambuf::xsputn(const char_type* s, std::streamsize max)
    {
        std::streamsize size = 0;
        while (size < max)
        {
            auto n = std::min(epptr() - pptr(), max - size);
            std::copy_n(s, n, pptr());
            pbump(n);
            s += n;
            size += n;
            if (size < max) overflow();
        }
        return size;
    }

    mpu401_streambuf::int_type mpu401_streambuf::overflow(int_type c)
    {
        if (pptr() == epptr())
        {
            if (cfg.use_irq)
            {
                this_thread::yield_while([this]
                {
                    check_irq_exception();
                    {
                        dpmi::interrupt_mask no_irq { };
                        do_sync();
                    }
                    return tx_ptr == pbase();
                });
            }
            else
            {
                this_thread::yield_while([this]
                {
                    do_sync();
                    return tx_ptr == pbase();
                });
            }
            std::copy(tx_ptr, pptr(), pbase());
            pbump(pbase() - tx_ptr);
            tx_ptr = pbase();
        }

        if (traits_type::not_eof(c)) sputc(c);
        return ~traits_type::eof();
    }

    void mpu401_streambuf::check_irq_exception()
    {
        if (irq_exception != nullptr) [[unlikely]]
        {
            std::exception_ptr e { nullptr };
            std::swap(e, irq_exception);
            std::rethrow_exception(e);
        }
    }
}
