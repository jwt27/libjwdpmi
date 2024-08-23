/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2017 - 2024 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <jw/dpmi/bda.h>
#include <jw/dpmi/alloc.h>
#include <jw/dpmi/irq_handler.h>
#include <jw/io/ioport.h>
#include <jw/io/io_error.h>
#include <jw/allocator_adaptor.h>
#include <jw/io/realtime_streambuf.h>
#include <jw/circular_queue.h>
#include <iostream>
#include <algorithm>
#include <deque>

namespace jw::io
{
    enum com_port
    {
        com1,
        com2,
        com3,
        com4
    };

    enum class rs232_parity : std::uint8_t
    {
        none  = 0b000,
        odd   = 0b001,
        even  = 0b011,
        mark  = 0b101,
        space = 0b111
    };

    struct rs232_config
    {
        port_num io_port;
        dpmi::irq_level irq;

        // Allowed values: 5, 6, 7, 8.
        std::uint8_t char_bits { 8 };

        // Allowed values: 1, 2.
        // When char_bits is set to 5, a value of 2 means 1.5 stop bits.
        std::uint8_t stop_bits { 1 };

        rs232_parity parity { rs232_parity::none };

        std::uint16_t baud_rate_divisor { 1 };

        enum : std::uint8_t
        {
            // No flow control.  RTS and DTR are held high to supply power
            // to a serial mouse.
            continuous,

            // Symmetric in-band flow control, not suitable for binary
            // transmission.  The XON/XOFF bytes are consumed and do not
            // appear in the input stream.  RTS and DTR are held high.
            xon_xoff,

            // Symmetric flow control, used with null-modem cables.  RTS (used
            // as RTR) is asserted while there is free space in the receive
            // buffer, and transmission only occurs when CTS is active.
            rtr_cts
        } flow_control { continuous };

        // On some boards, may select a secondary clock crystal.
        bool enable_aux_out1 { false };

        // When set, flush() enables the transmit interrupt and returns
        // immediately.  Otherwise, waits until the transmit buffer is
        // completely flushed.
        bool async_flush { true };

        // When set, a break condition (line held low for longer than one
        // character), is reported by returning EOF.  Otherwise, a '\0'
        // character is inserted in the stream.
        bool eof_on_break { true };

        std::size_t realtime_buffer_size { 128 };
        std::size_t transmit_buffer_size { 4_KB };
        std::size_t receive_buffer_size { 4_KB };

        // Try to reserve this much space for putback() / unget() operations.
        std::size_t putback_reserve { 0 };

        void set_com_port(com_port p)
        {
            io_port = find_io_port(p);
            irq = find_irq(p);
        }

        void set_baud_rate(auto rate)
        {
            auto d = std::div(115200, rate);
            if (d.rem != 0 or d.quot > std::numeric_limits<std::uint16_t>::max())
                throw std::invalid_argument { "Invalid baud rate." };
            baud_rate_divisor = d.quot;
        }

    private:
        port_num find_io_port(com_port p)
        {
            port_num port { 0 };
            if (p <= com4) port = dpmi::bda->read<std::uint16_t>(static_cast<unsigned>(p) * 2);
            if (port == 0) throw std::invalid_argument { "Invalid COM port." };
            return port;
        }

        dpmi::irq_level find_irq(com_port p)
        {
            switch (p)
            {
            case com1: case com3: return 4;
            case com2: case com4: return 3;
            default: return 0;
            }
        }
    };

    // 16550A UART driver, implemented as std::streambuf.  This must be
    // allocated in locked memory, which rs232_stream will do for you.
    // Transmission errors are reported via exceptions - underflow() may throw
    // one of the following: io::overflow, io::framing_error, io::parity_error.
    // The error applies to the first character read after clearing the
    // exception state.
    // A break, where the line is held low for longer than one character, is
    // reported by returning EOF.
    struct rs232_streambuf final : realtime_streambuf
    {
        rs232_streambuf(const rs232_config&);
        virtual ~rs232_streambuf();

        rs232_streambuf() = delete;
        rs232_streambuf(const rs232_streambuf&) = delete;
        rs232_streambuf(rs232_streambuf&&) = delete;

        // Ignores flow control.
        virtual void put_realtime(char_type) override;

        // Blocks until the entire output buffer is flushed, regardless of the
        // async_flush option.
        int force_sync();

    protected:
        virtual std::streamsize showmanyc() override;
        virtual int_type underflow() override;
        virtual int_type pbackfail(int_type = traits_type::eof()) override;
        virtual int_type overflow(int_type = traits_type::eof()) override;
        virtual int sync() override;

    private:
        template<typename T>
        using allocator = default_constructing_allocator_adaptor<dpmi::global_locked_pool_allocator<T>>;

        using tx_queue = dynamic_circular_queue<char_type, queue_sync::consumer_irq, allocator<char_type>>;
        using rx_queue = dynamic_circular_queue<char_type, queue_sync::producer_irq, allocator<char_type>>;

        struct error_mark
        {
            rx_queue::iterator pos;
            std::uint8_t status;
        };

        using error_queue = std::deque<error_mark, allocator<error_mark>>;

        int sync(bool);
        void do_setp(tx_queue::iterator) noexcept;
        tx_queue::iterator update_tx_stop() noexcept;
        void set_tx() noexcept;
        void set_rts(bool) noexcept;
        std::uint8_t read_status() noexcept;
        void do_sync(std::size_t = 0) noexcept;
        void wait();
        void irq_handler() noexcept;

        const port_num base;
        tx_queue realtime_buf;
        tx_queue tx_buf;
        rx_queue rx_buf;
        error_queue errors;
        error_mark* first_error { nullptr };
        tx_queue::const_iterator tx_stop;
        bool can_tx { true };
        bool can_rx { false };
        std::uint8_t modem_control_reg { };
        std::uint8_t line_status_reg { };
        std::uint8_t irq_enable_reg { };
        const bool eof_on_break;
        const bool async_flush;
        const decltype(rs232_config::flow_control) flow_control;
        const std::size_t putback_reserve;
        dpmi::irq_handler irq;

        struct irq_disable
        {
            irq_disable(rs232_streambuf*) noexcept;
            ~irq_disable() noexcept;

            rs232_streambuf* const self;
        };
    };

    // Serial port stream, using the rs232_streambuf.
    struct rs232_stream : std::iostream
    {
        rs232_stream(const rs232_config& c)
            : std::iostream { }
            , streambuf { new (locked) rs232_streambuf { c } }
        {
            this->init(streambuf.get());
        }

        rs232_streambuf* rdbuf() const noexcept
        {
            return streambuf.get();
        }

        rs232_stream& force_flush();

    private:
        std::unique_ptr<rs232_streambuf> streambuf;
    };
}
