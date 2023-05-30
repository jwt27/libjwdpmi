/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2023 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <iostream>
#include <algorithm>
#include <jw/dpmi/bda.h>
#include <jw/dpmi/alloc.h>
#include <jw/dpmi/irq_handler.h>
#include <jw/io/ioport.h>
#include <jw/io/io_error.h>

namespace jw::io
{
    enum com_port
    {
        com1,
        com2,
        com3,
        com4
    };

    struct rs232_config
    {
        port_num io_port;
        dpmi::irq_level irq;
        enum char_bits_t : std::uint8_t
        {
            char_5,
            char_6,
            char_7,
            char_8
        } char_bits { char_8 };
        enum stop_bits_t : std::uint8_t
        {
            stop_1,
            stop_2
        } stop_bits { stop_1 };
        enum parity_t : std::uint8_t
        {
            none = 0b000,
            odd = 0b001,
            even = 0b011,
            mark = 0b101,
            space = 0b111
        } parity { none };
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
}

#include <jw/io/detail/rs232.h>

namespace jw::io
{
    struct rs232_stream : std::iostream
    {
        rs232_stream(const rs232_config& c)
            : std::iostream { }
            , streambuf { new (locked) detail::rs232_streambuf { c } }
        {
            this->init(streambuf.get());
        }

        detail::rs232_streambuf* rdbuf() const noexcept
        {
            return streambuf.get();
        }

        rs232_stream& force_flush();

    private:
        std::unique_ptr<detail::rs232_streambuf> streambuf;
    };
}
