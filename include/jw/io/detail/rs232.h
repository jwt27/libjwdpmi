#pragma once
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
                rs232_streambuf(rs232_config p);
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
                void set_rts() noexcept
                {
                    if (config.force_dtr_rts_high) return;
                    auto r = modem_control.read();
                    auto r2 = r;
                    r2.dtr = true;
                    r2.rts = rx_ptr != rx_buf.end();
                    if (r.rts != r2.rts)
                    {
                        modem_control.write(r2);
                        //TODO: xon/xoff
                    }
                }

                void get(bool entire_fifo = false) noexcept
                {
                    if (getting.test_and_set()) return;
                    auto end = rx_ptr + std::min(entire_fifo ? 14 : 1, rx_buf.end() - rx_ptr);
                    while (rx_ptr < end) *(rx_ptr++) = get_one();
                    setg(rx_buf.begin(), gptr(), end);
                    getting.clear();
                }

                void put() noexcept
                {
                    //if (config.flow_control == rs232_config::xon_xoff && !cts) { put_one(xon); return; };
                    if (putting.test_and_set()) return;
                    auto end = tx_ptr + std::min(line_status.read().tx_fifo_empty ? 16 : 1, pptr() - tx_ptr);
                    for (; tx_ptr < end; ++tx_ptr)
                        if (!put_one(*tx_ptr)) break;
                    putting.clear();
                }

                char_type get_one() noexcept
                {
                    retry:
                    do { } while (!line_status.read().data_available);
                    auto c = data_port.read();
                    if (config.flow_control == rs232_config::xon_xoff)
                    {
                        if (c == xon) { cts = true; goto retry; }
                        if (c == xoff) { cts = false; goto retry; }
                    }
                    if (config.echo) sputc(c);
                    return c;
                }

                bool put_one(char_type c) noexcept
                {
                    if (!line_status.read().transmitter_empty) return false;
                    if (config.flow_control == rs232_config::rts_cts && !modem_status.read().cts) return false;
                    do { } while (!line_status.read().transmitter_empty);
                    data_port.write(c);
                    return true;
                }

                dpmi::irq_handler irq_handler { [&](auto ack) INTERRUPT
                {
                    auto id = irq_id.read();
                    if (!id.no_irq_pending)
                    {
                        switch (id.id)
                        {
                        case uart_irq_id_reg::data_available:
                            get(!id.timeout); break;
                        case uart_irq_id_reg::transmitter_empty:
                            put(); break;
                        case uart_irq_id_reg::line_status:
                            if (line_status.read().line_break) { cts = false; break; }
                        case uart_irq_id_reg::modem_status:
                            put(); break;
                        }
                        ack();
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
                std::atomic_flag getting { false };
                std::atomic_flag putting { false };
                bool cts { false };

                std::array<char_type, 1_KB> rx_buf;
                std::array<char_type, 1_KB> tx_buf;
                char_type* rx_ptr { rx_buf.data() };
                char_type* tx_ptr { tx_buf.data() };

                static const char_type xon = 0x11;
                static const char_type xoff = 0x13;

                static std::unordered_map<port_num, bool> com_port_use_map;

                struct irq_disable  // TODO: disable get/put irqs separately
                {
                    irq_disable(auto* p) noexcept : owner(p), reg(p->irq_enable.read()) { owner->irq_enable.write({ }); }
                    ~irq_disable() { owner->irq_enable.write(reg); }

                protected:
                    irq_disable() { }
                    rs232_streambuf* owner;
                    uart_irq_enable_reg reg;
                };
            };
        }
    }
}
