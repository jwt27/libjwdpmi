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

    struct [[gnu::packed]] uart_irq_enable_reg
    {
        bool data_available : 1;
        bool transmitter_empty : 1;
        bool line_status : 1;
        bool modem_status : 1;
        unsigned : 4;
    };

    struct [[gnu::packed]] uart_irq_id_reg
    {
        bool no_irq_pending : 1;
        enum
        {
            modem_status,
            transmitter_empty,
            data_available,
            line_status
        } id : 2;
        bool timeout : 1;
        unsigned : 2;
        unsigned fifo_enabled : 2;
    };

    struct [[gnu::packed]] uart_fifo_control_reg
    {
        bool enable_fifo : 1;
        bool clear_rx : 1;
        bool clear_tx : 1;
        bool dma_mode : 1;
        unsigned : 2;
        enum
        {
            bytes_1,
            bytes_4,
            bytes_8,
            bytes_14
        } irq_threshold : 2;
    };

    struct [[gnu::packed]] uart_line_control_reg
    {
        rs232_config::char_bits_t char_bits : 2;
        rs232_config::stop_bits_t stop_bits : 1;
        rs232_config::parity_t parity : 3;
        bool force_break : 1;
        bool divisor_access : 1;
    };

    struct [[gnu::packed]] uart_modem_control_reg
    {
        bool dtr : 1;
        bool rts : 1;
        bool aux_out1 : 1;
        bool aux_out2 : 1;
        bool loopback_mode : 1;
        unsigned : 3;
    };

    struct [[gnu::packed]] uart_line_status_reg
    {
        bool data_available : 1;
        bool overflow_error : 1;
        bool parity_error : 1;
        bool framing_error : 1;
        bool line_break : 1;
        bool transmitter_empty : 1;
        bool tx_fifo_empty : 1;
        unsigned fifo_contains_error : 1;
    };

    struct [[gnu::packed]] uart_modem_status_reg
    {
        bool delta_cts : 1;
        bool delta_dts : 1;
        bool delta_ri : 1;
        bool delta_dcd : 1;
        bool cts : 1;
        bool dsr : 1;
        bool ri : 1;
        bool dcd : 1;
    };

    static io_port<std::uint16_t>           rate_divisor(const rs232_config& c)     { return { c.io_port + 0 }; }
    static io_port<char>                    data(const rs232_config& c)             { return { c.io_port + 0 }; }
    static io_port<uart_irq_enable_reg>     irq_enable(const rs232_config& c)       { return { c.io_port + 1 }; }
    static in_port<uart_irq_id_reg>         irq_id(const rs232_config& c)           { return { c.io_port + 2 }; }
    static out_port<uart_fifo_control_reg>  fifo_control(const rs232_config& c)     { return { c.io_port + 2 }; }
    static io_port<uart_line_control_reg>   line_control(const rs232_config& c)     { return { c.io_port + 3 }; }
    static io_port<uart_modem_control_reg>  modem_control(const rs232_config& c)    { return { c.io_port + 4 }; }
    static in_port<uart_line_status_reg>    line_status(const rs232_config& c)      { return { c.io_port + 5 }; }
    static in_port<uart_modem_status_reg>   modem_status(const rs232_config& c)     { return { c.io_port + 6 }; }

    static uart_line_status_reg read_status(const rs232_config& cfg)
    {
        auto status = line_status(cfg).read();
        if (status.overflow_error)       throw io::overflow { "RS232 FIFO overflow" };
        if (status.parity_error)         throw parity_error { "RS232 parity error" };
        if (status.framing_error)        throw framing_error { "RS232 framing error" };
        if (status.line_break)           throw line_break { "RS232 line break detected" };
        //if (status.fifo_contains_error)  throw io_error { "RS232 FIFO contains error" };
        return status;
    }

    rs232_streambuf::rs232_streambuf(const rs232_config& c)
        : cfg { c }, irq { [this] { irq_handler(); } }
    {
        if (ports_used.contains(cfg.io_port)) throw std::runtime_error("COM port already in use.");

        uart_irq_enable_reg irqen { };
        irq_enable(cfg).write(irqen);

        setg(rx_buf.begin(), rx_buf.begin(), rx_buf.begin());
        setp(tx_buf.begin(), tx_buf.end());

        uart_line_control_reg lctrl { };
        lctrl.divisor_access = true;
        lctrl.char_bits = cfg.char_bits;
        lctrl.parity = cfg.parity;
        lctrl.stop_bits = cfg.stop_bits;
        line_control(cfg).write(lctrl);

        rate_divisor(cfg).write(cfg.baud_rate_divisor);

        lctrl.divisor_access = false;
        line_control(cfg).write(lctrl);

        uart_modem_control_reg mctrl { };
        mctrl.dtr = not cfg.force_dtr_rts_high; // note: dtr/rts are inverted
        mctrl.rts = not cfg.force_dtr_rts_high;
        mctrl.aux_out1 = cfg.enable_aux_out1;
        mctrl.aux_out2 = true;
        modem_control(cfg).write(mctrl);

        uart_fifo_control_reg fctrl { };
        fifo_control(cfg).write(fctrl);
        fctrl.enable_fifo = true;
        fctrl.clear_rx = true;
        fctrl.clear_tx = true;
        fctrl.irq_threshold = uart_fifo_control_reg::bytes_8;
        fifo_control(cfg).write(fctrl);

        if (irq_id(cfg).read().fifo_enabled != 0b11) throw device_not_found("16550A not detected"); // HACK

        irq.set_irq(cfg.irq);
        irq.enable();

        irqen.data_available = true;
        irqen.transmitter_empty = true;
        irqen.line_status = (cfg.flow_control == rs232_config::xon_xoff);
        irqen.modem_status = (cfg.flow_control == rs232_config::rts_cts);
        irq_enable(cfg).write(irqen);

        set_rts();
        ports_used.insert(cfg.io_port);
    }

    rs232_streambuf::~rs232_streambuf()
    {
        modem_control(cfg).write({ });
        irq_enable(cfg).write({ });
        irq.disable();
        ports_used.erase(cfg.io_port);
    }

    int rs232_streambuf::sync()
    {
        this_thread::yield_while([this]
        {
            {
                std::unique_lock lock { getting };
                if (read_status(cfg).data_available) underflow();
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
                    or not irq_enable(cfg).read().data_available
                    or not dpmi::irq_mask::enabled(cfg.irq))
                and read_status(cfg).data_available) get();
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
        if (cfg.force_dtr_rts_high) return;
        auto r = modem_control(cfg).read();
        auto r2 = r;
        r2.dtr = true;
        r2.rts = egptr() != rx_buf.end() - 8;
        if (r.rts != r2.rts)
        {
            modem_control(cfg).write(r2);
            //TODO: xon/xoff
        }
    }

    inline void rs232_streambuf::get(bool entire_fifo)
    {
        //irq_disable no_irq { this, irq_disable::get };
        dpmi::interrupt_mask no_irq { };
        auto size = std::min(entire_fifo ? 8 : 1, rx_buf.end() - rx_ptr);
        for (auto i = 0; i < size or read_status(cfg).data_available; ++i)
        {
            if (rx_ptr >= rx_buf.end()) throw io::overflow { "RS232 receive buffer overflow" };
            *(rx_ptr++) = get_one();
        }
        setg(rx_buf.begin(), gptr(), rx_ptr);
    }

    inline void rs232_streambuf::put()
    {
        //if (cfg.flow_control == rs232_config::xon_xoff && !cts) { put_one(xon); return; };
        //irq_disable no_irq { this, irq_disable::put };
        dpmi::interrupt_mask no_irq { };
        std::unique_lock<recursive_mutex> locked { putting, std::try_to_lock };
        if (not locked) return;
        auto size = std::min(read_status(cfg).tx_fifo_empty ? 16 : 1, pptr() - tx_ptr);
        for (auto i = 0; i < size or (tx_ptr < pptr() and read_status(cfg).transmitter_empty); ++tx_ptr, ++i)
            if (not put_one(*tx_ptr)) break;
    }

    inline char rs232_streambuf::get_one()
    {
    retry:
        try
        {
            do { } while (not read_status(cfg).data_available);
        }
        catch (const line_break&)
        {
            if (data(cfg).read() != 0) throw;
            goto retry;
        }
        auto c = data(cfg).read();
        if (cfg.flow_control == rs232_config::xon_xoff)
        {
            if (c == xon) { cts = true; goto retry; }
            if (c == xoff) { cts = false; goto retry; }
        }
        if (cfg.echo) sputc(c);
        return c;
    }

    inline bool rs232_streambuf::put_one(char_type c)
    {
        if (not read_status(cfg).transmitter_empty) return false;
        if (cfg.flow_control == rs232_config::rts_cts and not modem_status(cfg).read().cts) return false;
        this_thread::yield_while([this] { return not read_status(cfg).transmitter_empty; });
        data(cfg).write(c);
        return true;
    }

    inline void rs232_streambuf::irq_handler()
    {
        auto id = irq_id(cfg).read();
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
                    try { read_status(cfg); }
                    catch (const line_break&) { cts = false; break; }
                    [[fallthrough]];
                case uart_irq_id_reg::modem_status:
                    modem_status(cfg).read();
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
