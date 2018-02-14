/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <jw/io/rs232.h>
#include <jw/dpmi/irq_mask.h>

namespace jw
{
    namespace io
    {
        namespace detail
        {
            std::unordered_map<port_num, bool> rs232_streambuf::com_port_use_map { };

            rs232_streambuf::rs232_streambuf(rs232_config p)
                : config(p), 
                rate_divisor(p.io_port), data_port(p.io_port), 
                irq_enable(p.io_port + 1), 
                irq_id(p.io_port + 2), fifo_control(p.io_port + 2),
                line_control(p.io_port + 3), modem_control(p.io_port + 4),
                line_status(p.io_port + 5), modem_status(p.io_port + 6) 
            {
                if (com_port_use_map[config.io_port]) throw std::runtime_error("COM port already in use.");
                com_port_use_map[config.io_port] = true;

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
                fctrl.irq_threshold = uart_fifo_control_reg::bytes_14;
                fifo_control.write(fctrl);

                if (irq_id.read().fifo_enabled != 0b11) throw std::runtime_error("16550A not detected"); // HACK

                irq_handler.set_irq(config.irq);
                irq_handler.enable();

                irqen.data_available = true;
                irqen.transmitter_empty = true;
                irqen.line_status = (config.flow_control == rs232_config::xon_xoff);
                irqen.modem_status = (config.flow_control == rs232_config::rts_cts);
                irq_enable.write(irqen);

                set_rts();
            }

            rs232_streambuf::~rs232_streambuf() 
            {
                modem_control.write({ });
                irq_enable.write({ });
                irq_handler.disable();
                com_port_use_map[config.io_port] = false;
            }

            int rs232_streambuf::sync()
            {
                while (tx_ptr < pptr())
                {
                    overflow();
                    underflow();
                    thread::yield();
                }
                return 0;
            }

            std::streamsize rs232_streambuf::xsgetn(char_type * s, std::streamsize n)
            {
                std::streamsize max_n = 0;
                while (max_n < n && underflow() != traits_type::eof()) max_n = std::min(rx_ptr - gptr(), n);
                {
                    irq_disable no_irq { this };
                    std::copy_n(gptr(), max_n, s);
                    setg(rx_buf.begin(), gptr() + max_n, rx_ptr);
                }
                return max_n;
            }

            rs232_streambuf::int_type rs232_streambuf::underflow()
            {
                std::unique_lock<std::recursive_mutex> lock { getting };
                if (gptr() != rx_buf.begin()) std::copy(gptr(), rx_ptr, rx_buf.begin());
                rx_ptr = rx_buf.begin() + (rx_ptr - gptr());
                setg(rx_buf.begin(), rx_buf.begin(), rx_ptr);
                do 
                { 
                    get(); 
                    thread::yield();
                } while (rx_ptr == gptr());
                set_rts();
                return *gptr();
            }

            std::streamsize rs232_streambuf::xsputn(const char_type * s, std::streamsize n)
            {
                irq_disable no_irq { this };
                auto max_n = std::min(tx_buf.end() - pptr(), n);
                if (max_n < n) overflow();
                max_n = std::min(tx_buf.end() - pptr(), n);
                std::copy_n(s, max_n, pptr());
                setp(pptr() + max_n, tx_buf.end());
                return max_n;
            }

            rs232_streambuf::int_type rs232_streambuf::overflow(int_type c) 
            {
                std::unique_lock<std::recursive_mutex> lock { putting };
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
