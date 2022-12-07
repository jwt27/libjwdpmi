/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <mutex>
#include <unordered_set>
#include <jw/thread.h>
#include <jw/mutex.h>

namespace jw::io::detail
{
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

    struct rs232_streambuf : std::streambuf
    {
        rs232_streambuf(const rs232_config& p);
        virtual ~rs232_streambuf();

        rs232_streambuf() = delete;
        rs232_streambuf(const rs232_streambuf&) = delete;
        rs232_streambuf(rs232_streambuf&& m) = delete;

        std::string_view view() const
        {
            const char* const p = gptr();
            std::size_t size = egptr() - p;
            return { p, size };
        }

    protected:
        virtual int sync() override;
        virtual std::streamsize xsgetn(char_type* s, std::streamsize n) override;
        virtual int_type underflow() override;
        virtual std::streamsize xsputn(const char_type* s, std::streamsize n) override;
        virtual int_type overflow(int_type c = traits_type::eof()) override;

    private:
        void check_irq_exception();
        void set_rts() noexcept;
        uart_line_status_reg read_status();
        void get(bool entire_fifo = false);
        void put();
        char_type get_one();
        bool put_one(char_type c);
        void irq_handler();

        rs232_config config;
        dpmi::irq_handler irq;
        io_port <std::uint16_t> rate_divisor;
        io_port <byte> data_port;
        io_port <uart_irq_enable_reg> irq_enable;
        in_port <uart_irq_id_reg> irq_id;
        out_port<uart_fifo_control_reg> fifo_control;
        io_port <uart_line_control_reg> line_control;
        io_port <uart_modem_control_reg> modem_control;
        in_port <uart_line_status_reg> line_status;
        in_port <uart_modem_status_reg> modem_status;
        recursive_mutex getting, putting;
        std::exception_ptr irq_exception;
        bool cts { false };

        std::array<char_type, 1_KB> rx_buf;
        std::array<char_type, 1_KB> tx_buf;
        char_type* rx_ptr { rx_buf.data() };
        char_type* tx_ptr { tx_buf.data() };

        inline static std::unordered_set<port_num> ports_used;
    };
}
