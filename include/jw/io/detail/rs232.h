/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2023 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/allocator_adaptor.h>
#include <jw/dpmi/alloc.h>
#include <jw/io/realtime_streambuf.h>
#include <jw/circular_queue.h>
#include <deque>

namespace jw::io::detail
{
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

        using tx_queue = dynamic_circular_queue<char_type, queue_sync::read_irq, allocator<char_type>>;
        using rx_queue = dynamic_circular_queue<char_type, queue_sync::write_irq, allocator<char_type>>;

        struct error_mark
        {
            rx_queue::const_iterator pos;
            std::uint8_t status;
        };

        using error_queue = std::deque<error_mark, allocator<error_mark>>;

        int sync(bool);
        void do_setp(tx_queue::iterator) noexcept;
        void set_tx(bool) noexcept;
        void set_rts(bool) noexcept;
        std::uint8_t read_status() noexcept;
        void do_sync(std::size_t = 0) noexcept;
        void irq_handler() noexcept;

        const rs232_config cfg;
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
        dpmi::irq_handler irq;

        struct irq_disable
        {
            irq_disable(rs232_streambuf*) noexcept;
            ~irq_disable() noexcept;

            rs232_streambuf* const self;
        };
    };
}
