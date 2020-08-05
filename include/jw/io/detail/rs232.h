/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <mutex>
#include <unordered_set>
#include <jw/thread/thread.h>
#include <jw/thread/mutex.h>

namespace jw
{
    namespace io
    {
        namespace detail
        {
            struct[[gnu::packed]] uart_irq_enable_reg
            {
                bool data_available : 1;
                bool transmitter_empty : 1;
                bool line_status : 1;
                bool modem_status : 1;
                unsigned : 4;
            };

            struct[[gnu::packed]] uart_irq_id_reg
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

            struct[[gnu::packed]] uart_fifo_control_reg
            {
                bool enable_fifo : 1;
                bool clear_rx : 1;
                bool clear_tx : 1;
                bool dma_mode : 1; // WTF?
                unsigned : 2;
                enum
                {
                    bytes_1,
                    bytes_4,
                    bytes_8,
                    bytes_14
                } irq_threshold : 2;
            };

            struct[[gnu::packed]] uart_line_control_reg
            {
                rs232_config::char_bits_t char_bits : 2;
                rs232_config::stop_bits_t stop_bits : 1;
                rs232_config::parity_t parity : 3;
                bool force_break : 1;
                bool divisor_access : 1;
            };

            struct[[gnu::packed]] uart_modem_control_reg
            {
                bool dtr : 1;
                bool rts : 1;
                bool aux_out1 : 1;
                bool aux_out2 : 1;
                bool loopback_mode : 1;
                unsigned : 3;
            };

            struct[[gnu::packed]] uart_line_status_reg
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

            struct[[gnu::packed]] uart_modem_status_reg
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

            struct rs232_streambuf : public std::streambuf, dpmi::class_lock<rs232_streambuf>
            {
                rs232_streambuf(const rs232_config& p);
                virtual ~rs232_streambuf();

                rs232_streambuf() = delete;
                rs232_streambuf(const rs232_streambuf&) = delete;
                rs232_streambuf(rs232_streambuf&& m) = delete;
                //rs232_streambuf(rs232_streambuf&& m) : rs232_streambuf(m.config) { m.irq_handler.disable(); } // TODO: move constructor

            protected:
                virtual int sync() override;
                virtual std::streamsize xsgetn(char_type* s, std::streamsize n) override;
                virtual int_type underflow() override;
                virtual std::streamsize xsputn(const char_type* s, std::streamsize n) override;
                virtual int_type overflow(int_type c = traits_type::eof()) override;

            private:
                void check_irq_exception()
                {
                    if (__builtin_expect(irq_exception != nullptr, false))
                    {
                        auto e = std::move(irq_exception);
                        irq_exception = nullptr;
                        std::rethrow_exception(e);
                    }
                }

                void set_rts() noexcept
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

                uart_line_status_reg read_status()
                {
                    auto status = line_status.read();
                    if (status.overflow_error)       throw io::overflow { "RS232 FIFO overflow" };
                    if (status.parity_error)         throw parity_error { "RS232 parity error" };
                    if (status.framing_error)        throw framing_error { "RS232 framing error" };
                    if (status.line_break)           throw line_break { "RS232 line break detected" };
                    //if (status.fifo_contains_error)  throw io_error { "RS232 FIFO contains error" };
                    return status;
                }

                void get(bool entire_fifo = false)
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

                void put()
                {
                    //if (config.flow_control == rs232_config::xon_xoff && !cts) { put_one(xon); return; };
                    //irq_disable no_irq { this, irq_disable::put };
                    dpmi::interrupt_mask no_irq { };
                    std::unique_lock<thread::recursive_mutex> locked { putting, std::try_to_lock };
                    if (not locked) return;
                    auto size = std::min(read_status().tx_fifo_empty ? 16 : 1, pptr() - tx_ptr);
                    for (auto i = 0; i < size or (tx_ptr < pptr() and read_status().transmitter_empty); ++tx_ptr, ++i)
                        if (not put_one(*tx_ptr)) break;
                }

                char_type get_one()
                {
                retry:
                    try
                    {
                        do { } while(not read_status().data_available);
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

                bool put_one(char_type c)
                {
                    if (not read_status().transmitter_empty) return false;
                    if (config.flow_control == rs232_config::rts_cts and not modem_status.read().cts) return false;
                    thread::yield_while([this] { return not read_status().transmitter_empty; });
                    data_port.write(c);
                    return true;
                }

                dpmi::irq_handler irq_handler { [this]()
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
                } };

                rs232_config config;
                io_port <std::uint16_t> rate_divisor;
                io_port <byte> data_port;
                io_port <uart_irq_enable_reg> irq_enable;
                in_port <uart_irq_id_reg> irq_id;
                out_port<uart_fifo_control_reg> fifo_control;
                io_port <uart_line_control_reg> line_control;
                io_port <uart_modem_control_reg> modem_control;
                in_port <uart_line_status_reg> line_status;
                in_port <uart_modem_status_reg> modem_status;
                thread::recursive_mutex getting, putting;
                std::exception_ptr irq_exception;
                bool cts { false };

                std::array<char_type, 1_KB> rx_buf;
                std::array<char_type, 1_KB> tx_buf;
                char_type* rx_ptr { rx_buf.data() };
                char_type* tx_ptr { tx_buf.data() };

                static const char_type xon = 0x11;
                static const char_type xoff = 0x13;

                inline static std::unordered_set<port_num> ports_used;

            protected:
                /*
                struct irq_disable  // TODO: needs fixing
                {
                    enum which
                    {
                        get = 0b1,
                        put = 0b10,
                        line = 0b100,
                        modem = 0b1000,
                        all = 0b1111
                    };

                    irq_disable(auto* p, auto type = all) noexcept : owner(p), reg(p->irq_enable.read())
                    {
                        uart_irq_enable_reg r { reg };
                        r.data_available = not (type & get);
                        r.transmitter_empty = not (type & put);
                        r.line_status = not (type & line);
                        r.modem_status = not (type & modem);
                        owner->irq_enable.write(r);
                    }
                    ~irq_disable() { owner->irq_enable.write(reg); }

                protected:
                    irq_disable() { }
                    rs232_streambuf* owner;
                    uart_irq_enable_reg reg;
                };*/
            };
        }
    }
}
