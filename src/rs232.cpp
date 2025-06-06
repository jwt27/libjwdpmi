/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2017 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#include <jw/io/rs232.h>
#include <jw/thread.h>
#include <jw/branchless.h>
#include <bit>
#include <vector>

namespace jw::io
{
    struct [[gnu::packed]] uart_irq_id
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

    struct [[gnu::packed]] uart_fifo_control
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

    struct [[gnu::packed]] uart_modem_status
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

    struct [[gnu::packed]] uart_line_control
    {
        unsigned char_bits : 2;
        unsigned stop_bits : 1;
        rs232_parity parity : 3;
        bool force_break : 1;
        bool divisor_access : 1;
    };

    static_assert (sizeof(uart_irq_id) == 1);
    static_assert (sizeof(uart_fifo_control) == 1);
    static_assert (sizeof(uart_modem_status) == 1);
    static_assert (sizeof(uart_line_control) == 1);

    enum class modem_control : std::uint8_t
    {
        dtr                 = 0b00000001,
        rts                 = 0b00000010,
        aux_out1            = 0b00000100,
        aux_out2            = 0b00001000,   // Disables IRQ line
        loopback_mode       = 0b00010000
    };

    enum class line_status : std::uint8_t
    {
        data_available      = 0b00000001,
        overflow_error      = 0b00000010,
        parity_error        = 0b00000100,
        framing_error       = 0b00001000,
        line_break          = 0b00010000,
        transmitter_empty   = 0b00100000,
        tx_fifo_empty       = 0b01000000,
        fifo_contains_error = 0b10000000,

        any_errors = overflow_error | parity_error | framing_error | line_break
    };

    enum class irq_enable : std::uint8_t
    {
        data_available      = 0b00000001,
        transmitter_empty   = 0b00000010,
        line_status         = 0b00000100,
        modem_status        = 0b00001000
    };

#   define MAKE_ENUM_BITOPS(T) \
    static constexpr std::uint8_t operator~ (T a) noexcept { return ~static_cast<std::uint8_t>(a); } \
    static constexpr std::uint8_t operator& (T a, T b) noexcept { return static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b); } \
    static constexpr std::uint8_t operator| (T a, T b) noexcept { return static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b); } \
    static constexpr std::uint8_t operator& (std::uint8_t a, T b) noexcept { return a & static_cast<std::uint8_t>(b); } \
    static constexpr std::uint8_t operator| (std::uint8_t a, T b) noexcept { return a | static_cast<std::uint8_t>(b); } \
    static constexpr std::uint8_t& operator&=(std::uint8_t& a, T b) noexcept { return a = a & static_cast<std::uint8_t>(b); } \
    static constexpr std::uint8_t& operator|=(std::uint8_t& a, T b) noexcept { return a = a | static_cast<std::uint8_t>(b); }

    MAKE_ENUM_BITOPS(line_status);
    MAKE_ENUM_BITOPS(modem_control);
    MAKE_ENUM_BITOPS(irq_enable);

#   undef MAKE_ENUM_BITOPS

    static constexpr char xon = 0x11;
    static constexpr char xoff = 0x13;

    static constinit std::vector<port_num>* ports_used { nullptr };

    static io_port<std::uint16_t>       rate_divisor_port(port_num base)     { return { base + 0 }; }
    static io_port<char>                data_port(port_num base)             { return { base + 0 }; }
    static io_port<std::uint8_t>        irq_enable_port(port_num base)       { return { base + 1 }; }
    static in_port<uart_irq_id>         irq_id_port(port_num base)           { return { base + 2 }; }
    static out_port<uart_fifo_control>  fifo_control_port(port_num base)     { return { base + 2 }; }
    static io_port<uart_line_control>   line_control_port(port_num base)     { return { base + 3 }; }
    static io_port<std::uint8_t>        modem_control_port(port_num base)    { return { base + 4 }; }
    static in_port<std::uint8_t>        line_status_port(port_num base)      { return { base + 5 }; }
    static in_port<uart_modem_status>   modem_status_port(port_num base)     { return { base + 6 }; }

    inline rs232_streambuf::irq_disable::irq_disable(rs232_streambuf* p) noexcept
        : self { p }
    {
        irq_enable_port(self->base).write({ });
    }

    inline rs232_streambuf::irq_disable::~irq_disable() noexcept
    {
        irq_enable_port(self->base).write(self->irq_enable_reg);
    }

    rs232_streambuf::rs232_streambuf(const rs232_config& cfg)
        : base { cfg.io_port }
        , realtime_buf { cfg.realtime_buffer_size }
        , tx_buf { cfg.transmit_buffer_size }
        , rx_buf { cfg.receive_buffer_size }
        , eof_on_break { cfg.eof_on_break }
        , async_flush { cfg.async_flush }
        , flow_control { cfg.flow_control }
        , putback_reserve { cfg.putback_reserve }
        , irq { [this] { irq_handler(); }, dpmi::no_auto_eoi }
    {
        if (cfg.char_bits < 5 or cfg.char_bits > 8)
            throw std::invalid_argument { "RS232: Invalid value for char_bits" };

        if (cfg.stop_bits < 1 or cfg.stop_bits > 2)
            throw std::invalid_argument { "RS232: Invalid value for stop_bits" };

        if (not ports_used)
            ports_used = new std::vector<port_num>;

        finally cleanup { []
        {
            if (ports_used->empty())
            {
                delete ports_used;
                ports_used = nullptr;
            }
        } };

        for (auto used : *ports_used)
            if (base == used)
                throw std::invalid_argument { "COM port already in use." };

        irq_enable_port(base).write({ });

        auto* const rx = rx_buf.consumer();
        auto* const tx = tx_buf.producer();
        const auto rx_begin = rx->begin();
        const auto tx_begin = tx->fill();

        setg(&*rx_begin, &*rx_begin, &*rx_begin);
        do_setp(tx_begin);
        tx_stop = tx_begin;

        uart_line_control lctrl { };
        lctrl.divisor_access = true;
        lctrl.char_bits = cfg.char_bits - 5;
        lctrl.stop_bits = cfg.stop_bits - 1;
        lctrl.parity = cfg.parity;
        line_control_port(base).write(lctrl);

        rate_divisor_port(base).write(cfg.baud_rate_divisor);

        lctrl.divisor_access = false;
        line_control_port(base).write(lctrl);

        uart_fifo_control fctrl { };
        fifo_control_port(base).write(fctrl);
        fctrl.enable_fifo = true;
        fctrl.clear_rx = true;
        fctrl.clear_tx = true;
        fctrl.irq_threshold = uart_fifo_control::bytes_8;
        fifo_control_port(base).write(fctrl);

        irq_enable_reg = 0 | irq_enable::data_available;
        if (flow_control == rs232_config::rtr_cts)
            irq_enable_reg |= irq_enable::modem_status;

        {
            dpmi::interrupt_mask no_irq { };

            irq_enable_port(base).write(irq_enable_reg);

            uart_irq_id id;
            do
            {
                line_status_port(base).read();
                modem_status_port(base).read();
                data_port(base).read();
                id = irq_id_port(base).read();
            } while (not id.no_irq_pending);
            if (id.fifo_enabled != 0b11) throw device_not_found { "16550A not detected" };

            irq.assign(cfg.irq);
            irq.enable();

            set_rts(true);
            set_tx();

            irq_enable_port(base).write(irq_enable_reg);

            modem_control_reg = modem_control::rts | modem_control::dtr | modem_control::aux_out2;
            if (cfg.enable_aux_out1) modem_control_reg |= modem_control::aux_out1;
            modem_control_port(base).write(modem_control_reg);
        }

        ports_used->push_back(base);
    }

    rs232_streambuf::~rs232_streambuf()
    {
        can_tx = true;
        force_sync();
        irq_enable_port(base).write({ });
        modem_control_reg &= ~(modem_control::dtr | modem_control::rts);
        modem_control_port(base).write(modem_control_reg);

        *std::ranges::find(*ports_used, base) = ports_used->back();
        ports_used->pop_back();
        if (ports_used->empty())
        {
            delete ports_used;
            ports_used = nullptr;
        }
    }

    void rs232_streambuf::put_realtime(char_type c)
    {
        auto* const rt = realtime_buf.producer();
        this_thread::yield_while([rt] { return rt->full(); });
        rt->try_push_back(c);
        update_tx_stop();
    }

    std::streamsize rs232_streambuf::showmanyc()
    {
        auto* const rx = rx_buf.consumer();
        const auto pos = rx->iterator_from_pointer(gptr());
        auto end = rx->cend();

        if (const auto* const err = volatile_load(&first_error)) [[unlikely]]
        {
            if (pos == err->pos)
                return -1;
            end = min(end, err->pos);
        }
        return pos.distance_to(end);
    }

    rs232_streambuf::int_type rs232_streambuf::underflow()
    {
        auto* const rx = rx_buf.consumer();

    retry:
        const auto pos = rx->iterator_from_pointer(gptr());
        rx->pop_front_to(clamp_add(pos, -putback_reserve, rx->begin(), pos));
        auto* new_end = rx->contiguous_end(pos);

        if (auto* const err = volatile_load(&first_error)) [[unlikely]]
        {
            auto pop = [this, err]
            {
                if (err->status & line_status::any_errors)
                    return;

                dpmi::interrupt_mask no_irq { };
                asm ("" ::: "memory");
                errors.pop_front();
                if (not errors.empty())
                    first_error = &errors.front();
                else
                    first_error = nullptr;
            };

            if (pos != err->pos)
            {
                if (pos.distance_to(err->pos) < pos.distance_to(rx->iterator_from_pointer(new_end)))
                    new_end = &*err->pos;
            }
            else if (err->status & line_status::overflow_error)
            {
                err->status &= ~line_status::overflow_error;
                pop();
                throw io::overflow { "RS-232 receive buffer overflow" };
            }
            else if (err->status & line_status::line_break)
            {
                err->status = 0;
                pop();
                if (eof_on_break)
                    return traits_type::eof();
                else
                    goto retry;
            }
            else if (err->status & line_status::framing_error)
            {
                err->status &= ~line_status::framing_error;
                pop();
                throw framing_error { "RS-232 framing error" };
            }
            else if (err->status & line_status::parity_error)
            {
                err->status &= ~line_status::parity_error;
                pop();
                throw parity_error { "RS-232 parity error" };
            }
        }

        if (new_end == &*pos)
        {
            wait();
            goto retry;
        }
        setg(rx->contiguous_begin(pos), &*pos, new_end);
        return traits_type::to_int_type(*gptr());
    }

    rs232_streambuf::int_type rs232_streambuf::pbackfail(int_type c)
    {
        if (eback() < gptr())
        {
            gbump(-1);
            traits_type::assign(*gptr(), traits_type::to_char_type(c));
            return traits_type::not_eof(c);
        }

        auto* const rx = rx_buf.consumer();
        auto i = rx->iterator_from_pointer(gptr());
        if (rx->begin().distance_to(i) > 0)
        {
            --i;
            setg(rx->contiguous_begin(i), &*i, rx->contiguous_end(i));
            traits_type::assign(*gptr(), traits_type::to_char_type(c));
            return traits_type::not_eof(c);
        }
        return traits_type::eof();
    }

    rs232_streambuf::int_type rs232_streambuf::overflow(int_type c)
    {
        auto* const tx = tx_buf.producer();
        const auto pos = update_tx_stop();

        if (pos == tx->cend())
            while (tx->full())
                wait();

        tx->fill();
        do_setp(pos);

        if (not traits_type::eq_int_type(c, traits_type::eof()))
        {
            traits_type::assign(*pptr(), traits_type::to_char_type(c));
            pbump(1);
        }

        return traits_type::not_eof(c);
    }

    int rs232_streambuf::force_sync()
    {
        return sync(true);
    }

    int rs232_streambuf::sync()
    {
        return sync(async_flush);
    }

    inline int rs232_streambuf::sync(bool force)
    {
        auto* const tx = tx_buf.producer();
        const auto pos = update_tx_stop();

        if (force)
            while (tx->begin() != pos)
                wait();

        tx->fill();
        do_setp(pos);
        return 0;
    }

    inline void rs232_streambuf::do_setp(tx_queue::iterator i) noexcept
    {
        // Split the TX buffer into smaller chunks, so tx_stop is updated more
        // frequently.
        auto* const tx = tx_buf.producer();
        auto* const p = &*i;
        setp(p, min(p + max<std::size_t>((tx->max_size() + 1) / 8, 1u), tx->contiguous_end(i)));
    }

    inline rs232_streambuf::tx_queue::iterator rs232_streambuf::update_tx_stop() noexcept
    {
        irq_disable no_irq { this };
        auto i = tx_buf.producer()->iterator_from_pointer(pptr());
        tx_stop = i;
        set_tx();
        return i;
    }

    // Enable or disable the TX interrupt.
    // Assumes IRQ is disabled!
    inline void rs232_streambuf::set_tx() noexcept
    {
        bool enable = tx_buf.consumer()->begin() != tx_stop;
        enable &= can_tx | closing;
        enable |= not realtime_buf.consumer()->empty();
        if (enable)
            irq_enable_reg |= irq_enable::transmitter_empty;
        else
            irq_enable_reg &= ~irq_enable::transmitter_empty;

        // Will be written out by ~irq_disable().
    }

    // Update the RTS pin or send XON/XOFF.
    // Assumes IRQ is disabled!
    inline void rs232_streambuf::set_rts(bool rts) noexcept
    {
        if (flow_control == rs232_config::continuous)
            return;

        rts &= not closing;
        if (can_rx == rts)
            return;
        can_rx = rts;

        if (flow_control == rs232_config::xon_xoff)
        {
            realtime_buf.producer()->try_push_back(rts ? xon : xoff);
        }
        else if (flow_control == rs232_config::rtr_cts)
        {
            if (rts) modem_control_reg |= modem_control::rts;
            else modem_control_reg &= ~modem_control::rts;
            modem_control_port(base).write(modem_control_reg);
        }
    }

    // Read the line status register.
    // Assumes IRQ is disabled!
    inline std::uint8_t rs232_streambuf::read_status() noexcept
    {
        const auto s = line_status_port(base).read();
        line_status_reg |= s & line_status::any_errors;
        return line_status_reg | s;
    };

    inline void rs232_streambuf::wait()
    {
#   ifndef NDEBUG
        if (dpmi::in_irq_context())
        {
            irq_disable no_irq { this };
            do_sync();
        }
        else
#   endif
        {
            this_thread::yield();
        }
    }

    // Assumes IRQ is disabled!
    void rs232_streambuf::do_sync(std::size_t rx_minimum) noexcept
    {
        auto* const rx = rx_buf.producer();
        auto* const tx = tx_buf.consumer();
        auto* const realtime = realtime_buf.consumer();
        std::size_t sent = 16;
        std::size_t received = 0;
        bool overflow = false;
        std::uint8_t status = read_status();

        auto add_error_mark = [this](auto pos, auto status)
        {
            if (errors.empty() or errors.back().pos != pos)
                errors.emplace_back(pos, static_cast<std::uint8_t>(status));
            else
                errors.back().status |= static_cast<std::uint8_t>(status);
            first_error = &errors.front();
        };

        auto not_xon_xoff = [&](char c)
        {
            if (status & line_status::any_errors) return true;
            if (flow_control != rs232_config::xon_xoff) [[likely]] return true;
            if (not (c == xon or c == xoff)) [[likely]] return true;
            can_tx = c == xon;
            return false;
        };

        if (status & line_status::overflow_error) [[unlikely]]
        {
            // Overflow bit applies to the end of the FIFO.  We must
            // now read the whole FIFO and place an error mark at the
            // end.
            rx_minimum = 16;
            overflow = true;
            status &= ~line_status::overflow_error;
            goto get;
        }
        else if (status & line_status::data_available) [[likely]]
            goto get;
        else
            goto put;

        do
        {
            while ((status = read_status()) & line_status::data_available) [[likely]]
            {
            get:
                const auto c = data_port(base).read();
                line_status_reg = 0;

                if (not_xon_xoff(c)) [[likely]]
                {
                    if ((status & line_status::line_break) and eof_on_break)
                        add_error_mark(rx->end(), status);
                    else
                    {
                        const auto i = rx->try_append(1, c);
                        if (not i) [[unlikely]]
                            add_error_mark(rx->end(), line_status::overflow_error);
                        else if (status & line_status::any_errors) [[unlikely]]
                            add_error_mark(*i, status);
                    }
                }

                ++received;
            }

        put:
            if (status & line_status::transmitter_empty)
                sent = 0;

            const auto n = min<std::size_t>(16u - sent, realtime->size());
            if (n > 0)
            {
                char buf[16];
                std::copy_n(realtime->begin(), 16, &*buf);
                realtime->pop_front(n);
                data_port(base).write(buf, n);
                sent += n;
            }

            if (can_tx) [[likely]]
            {
                const auto n = min<std::size_t>(16u - sent, tx->begin().distance_to(tx_stop));
                if (n > 0)
                {
                    char buf[16];
                    std::copy_n(tx->begin(), 16, &*buf);
                    tx->pop_front(n);
                    data_port(base).write(buf, n);
                    sent += n;
                }
            }
        } while (received < rx_minimum);

        if (overflow) [[unlikely]]
            add_error_mark(rx->end() - (received - 16), line_status::overflow_error);

        set_rts(rx->max_size() - rx->size() > 32);
        set_tx();
    }

    inline void rs232_streambuf::irq_handler() noexcept
    {
        const auto id = irq_id_port(base).read();
        if (id.no_irq_pending) [[unlikely]]
            return;

        irq_disable no_irq { this };
        dpmi::irq_handler::acknowledge();

        switch (id.id)
        {
        case uart_irq_id::modem_status:
            {
                const auto status = modem_status_port(base).read();
                if (not status.delta_cts) break;
                can_tx = status.cts;
            }
            [[fallthrough]];

        case uart_irq_id::line_status:
        case uart_irq_id::transmitter_empty:
            do_sync();
            break;

        case uart_irq_id::data_available:
            do_sync(id.timeout ? 1 : 8);
        }
    }

    rs232_stream& rs232_stream::force_flush()
    {
        std::ostream::sentry ok { *this };
        if (not ok) return *this;
        try
        {
            streambuf->force_sync();
        }
        catch (const abi::__forced_unwind&) { throw; }
        catch (...) { _M_setstate(badbit); }
        return *this;
    }
}
