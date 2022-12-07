/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <jw/io/rs232.h>
#include <jw/dpmi/irq_mask.h>

namespace jw::io::detail
{
    static constexpr char xon = 0x11;
    static constexpr char xoff = 0x13;

    rs232_streambuf::rs232_streambuf(const rs232_config& p)
        : config { p }, irq { [this] { irq_handler(); } },
        rate_divisor(p.io_port), data_port(p.io_port),
        irq_enable(p.io_port + 1),
        irq_id(p.io_port + 2), fifo_control(p.io_port + 2),
        line_control(p.io_port + 3), modem_control(p.io_port + 4),
        line_status(p.io_port + 5), modem_status(p.io_port + 6)
    {
        if (ports_used.contains(config.io_port)) throw std::runtime_error("COM port already in use.");

        uart_irq_enable_reg irqen { };
        irq_enable.write(irqen);

        setg(rx_buf.begin(), rx_buf.begin(), rx_buf.begin());
        setp(tx_buf.begin(), tx_buf.end());

        uart_line_control_reg lctrl { };
        lctrl.divisor_access = true;
        lctrl.char_bits = config.char_bits;
        lctrl.parity = config.parity;
        lctrl.stop_bits = config.stop_bits;
        line_control.write(lctrl);

        rate_divisor.write(config.baud_rate_divisor);

        lctrl.divisor_access = false;
        line_control.write(lctrl);

        uart_modem_control_reg mctrl { };
        mctrl.dtr = !config.force_dtr_rts_high; // note: dtr/rts are inverted
        mctrl.rts = !config.force_dtr_rts_high;
        mctrl.aux_out1 = config.enable_aux_out1;
        mctrl.aux_out2 = true;
        modem_control.write(mctrl);

        uart_fifo_control_reg fctrl { };
        fifo_control.write(fctrl);
        fctrl.enable_fifo = true;
        fctrl.clear_rx = true;
        fctrl.clear_tx = true;
        fctrl.irq_threshold = uart_fifo_control_reg::bytes_8;
        fifo_control.write(fctrl);

        if (irq_id.read().fifo_enabled != 0b11) throw device_not_found("16550A not detected"); // HACK

        irq.set_irq(config.irq);
        irq.enable();

        irqen.data_available = true;
        irqen.transmitter_empty = true;
        irqen.line_status = (config.flow_control == rs232_config::xon_xoff);
        irqen.modem_status = (config.flow_control == rs232_config::rts_cts);
        irq_enable.write(irqen);

        set_rts();
        ports_used.insert(config.io_port);
    }

    rs232_streambuf::~rs232_streambuf()
    {
        modem_control.write({ });
        irq_enable.write({ });
        irq.disable();
        ports_used.erase(config.io_port);
    }

    int rs232_streambuf::sync()
    {
        this_thread::yield_while([this]
        {
            {
                std::unique_lock lock { getting };
                if (read_status().data_available) underflow();
            }
            overflow();
            return tx_ptr < pptr();
        });
        return 0;
    }

    std::streamsize rs232_streambuf::xsgetn(char_type* s, std::streamsize n)
    {
        std::unique_lock lock { getting };
        std::streamsize max_n;
        this_thread::yield_while([&]
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

    // rx_buf:  xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
    //          ^               ^       ^       ^
    //          rx_buf.begin()  gptr()  rx_ptr  tx_buf.end()
    //          == eback()      |       |
    //                          |       +- one past last received char (== egptr())
    //                          +- next char to get

    rs232_streambuf::int_type rs232_streambuf::underflow()
    {
        std::unique_lock lock { getting };
        auto qsize = rx_buf.size() / 4;
        if (rx_ptr > rx_buf.begin() + qsize * 3)
        {
            dpmi::interrupt_mask no_irq { };
            auto offset = gptr() - (rx_buf.begin() + qsize);
            std::copy(rx_buf.begin() + offset, rx_ptr, rx_buf.begin());
            rx_ptr -= offset;
            setg(rx_buf.begin(), rx_buf.begin() + qsize, rx_ptr);
        }
        set_rts();
        do
        {
            check_irq_exception();
            if ((not dpmi::interrupts_enabled()
                    or not irq_enable.read().data_available
                    or not dpmi::irq_mask::enabled(config.irq))
                and read_status().data_available) get();
            else this_thread::yield();
        } while (gptr() == rx_ptr);
        return *gptr();
    }

    std::streamsize rs232_streambuf::xsputn(const char_type* s, std::streamsize n)
    {
        std::unique_lock lock { putting };
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

    // tx_buf:  xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
    //          ^               ^       ^       ^
    //          tx_buf.begin()  tx_ptr  pptr()  epptr()
    //                          |       |
    //                          |       +- current put pointer
    //                          +- next character to send

    rs232_streambuf::int_type rs232_streambuf::overflow(int_type c)
    {
        std::unique_lock lock { putting };
        this_thread::yield_while([this]
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

    inline void rs232_streambuf::check_irq_exception()
    {
        if (irq_exception != nullptr) [[unlikely]]
        {
            auto e = std::move(irq_exception);
            irq_exception = nullptr;
            std::rethrow_exception(e);
        }
    }

    inline void rs232_streambuf::set_rts() noexcept
    {
        if (config.force_dtr_rts_high) return;
        auto r = modem_control.read();
        auto r2 = r;
        r2.dtr = true;
        r2.rts = egptr() != rx_buf.end() - 8;
        if (r.rts != r2.rts)
        {
            modem_control.write(r2);
            //TODO: xon/xoff
        }
    }

    inline uart_line_status_reg rs232_streambuf::read_status()
    {
        auto status = line_status.read();
        if (status.overflow_error)       throw io::overflow { "RS232 FIFO overflow" };
        if (status.parity_error)         throw parity_error { "RS232 parity error" };
        if (status.framing_error)        throw framing_error { "RS232 framing error" };
        if (status.line_break)           throw line_break { "RS232 line break detected" };
        //if (status.fifo_contains_error)  throw io_error { "RS232 FIFO contains error" };
        return status;
    }

    inline void rs232_streambuf::get(bool entire_fifo)
    {
        //irq_disable no_irq { this, irq_disable::get };
        dpmi::interrupt_mask no_irq { };
        auto size = std::min(entire_fifo ? 8 : 1, rx_buf.end() - rx_ptr);
        for (auto i = 0; i < size or read_status().data_available; ++i)
        {
            if (rx_ptr >= rx_buf.end()) throw io::overflow { "RS232 receive buffer overflow" };
            *(rx_ptr++) = get_one();
        }
        setg(rx_buf.begin(), gptr(), rx_ptr);
    }

    inline void rs232_streambuf::put()
    {
        //if (config.flow_control == rs232_config::xon_xoff && !cts) { put_one(xon); return; };
        //irq_disable no_irq { this, irq_disable::put };
        dpmi::interrupt_mask no_irq { };
        std::unique_lock<recursive_mutex> locked { putting, std::try_to_lock };
        if (not locked) return;
        auto size = std::min(read_status().tx_fifo_empty ? 16 : 1, pptr() - tx_ptr);
        for (auto i = 0; i < size or (tx_ptr < pptr() and read_status().transmitter_empty); ++tx_ptr, ++i)
            if (not put_one(*tx_ptr)) break;
    }

    inline char rs232_streambuf::get_one()
    {
    retry:
        try
        {
            do { } while (not read_status().data_available);
        }
        catch (const line_break&)
        {
            if (data_port.read() != 0) throw;
            goto retry;
        }
        auto c = data_port.read();
        if (config.flow_control == rs232_config::xon_xoff)
        {
            if (c == xon) { cts = true; goto retry; }
            if (c == xoff) { cts = false; goto retry; }
        }
        if (config.echo) sputc(c);
        return c;
    }

    inline bool rs232_streambuf::put_one(char_type c)
    {
        if (not read_status().transmitter_empty) return false;
        if (config.flow_control == rs232_config::rts_cts and not modem_status.read().cts) return false;
        this_thread::yield_while([this] { return not read_status().transmitter_empty; });
        data_port.write(c);
        return true;
    }

    inline void rs232_streambuf::irq_handler()
    {
        auto id = irq_id.read();
        if (not id.no_irq_pending)
        {
            dpmi::irq_handler::acknowledge();
            try
            {
                switch (id.id)
                {
                case uart_irq_id_reg::data_available:
                    get(not id.timeout); break;
                case uart_irq_id_reg::transmitter_empty:
                    put(); break;
                case uart_irq_id_reg::line_status:
                    try { read_status(); }
                    catch (const line_break&) { cts = false; break; }
                    [[fallthrough]];
                case uart_irq_id_reg::modem_status:
                    modem_status.read();
                    put(); break;
                }
            }
            catch (...)
            {
                irq_exception = std::current_exception();
            }
        }
        set_rts();
    }
}
