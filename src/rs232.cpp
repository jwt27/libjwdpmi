/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <jw/io/rs232.h>
#include <jw/dpmi/irq_mask.h>

namespace jw
{
    namespace io
    {
        namespace detail
        {
            rs232_streambuf::rs232_streambuf(const rs232_config& p)
                : config(p), 
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

                if (irq_id.read().fifo_enabled != 0b11) throw std::runtime_error("16550A not detected"); // HACK

                irq_handler.set_irq(config.irq);
                irq_handler.enable();

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
                irq_handler.disable();
                ports_used.erase(config.io_port);
            }

            int rs232_streambuf::sync()
            {
                thread::yield_while([this]
                {
                    {
                        std::unique_lock<std::recursive_mutex> lock { getting };
                        if (read_status().data_available) underflow();
                    }
                    overflow();
                    return tx_ptr < pptr();
                });
                return 0;
            }

            std::streamsize rs232_streambuf::xsgetn(char_type* s, std::streamsize n)
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

            // rx_buf:  xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
            //          ^               ^       ^       ^
            //          rx_buf.begin()  gptr()  rx_ptr  tx_buf.end()
            //          == eback()      |       |
            //                          |       +- one past last received char (== egptr())
            //                          +- next char to get

            rs232_streambuf::int_type rs232_streambuf::underflow()
            {
                //std::clog << "underflow() avail=" << std::boolalpha << read_status().data_available;
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
                set_rts();
                do
                {
                    check_irq_exception();
                    if (not dpmi::interrupt_mask::enabled() or
                        not irq_enable.read().data_available or
                        not dpmi::irq_mask::enabled(config.irq))
                        get();
                    else thread::yield();
                    //std::clog << '.';
                } while (gptr() == rx_ptr);
                //std::clog << ". done. got: " << std::string_view { gptr(), egptr() - gptr() } << "\n";
                return *gptr();
            }

            std::streamsize rs232_streambuf::xsputn(const char_type* s, std::streamsize n)
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

            // tx_buf:  xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
            //          ^               ^       ^       ^
            //          tx_buf.begin()  tx_ptr  pptr()  epptr()
            //                          |       |
            //                          |       +- current put pointer
            //                          +- next character to send

            rs232_streambuf::int_type rs232_streambuf::overflow(int_type c) 
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
