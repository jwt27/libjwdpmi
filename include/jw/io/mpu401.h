/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2017 - 2023 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <jw/io/ioport.h>
#include <jw/io/io_error.h>
#include <jw/io/realtime_streambuf.h>
#include <jw/dpmi/irq_handler.h>
#include <jw/allocator_adaptor.h>
#include <jw/circular_queue.h>
#include <jw/common.h>
#include <iostream>
#include <deque>

namespace jw::io
{
    struct mpu401_config
    {
        port_num port { 0x330 };

        // If not set, only polling is used.
        std::optional<dpmi::irq_level> irq { 9 };

        std::size_t receive_buffer_size { 1_KB };
        std::size_t transmit_buffer_size { 1_KB };

        // Try to reserve this much space for putback() / unget() operations.
        std::size_t putback_reserve { 0 };
    };

    // Roland MPU-401 driver, operating in UART mode.  If mpu401_config.irq is
    // set, this must be allocated in locked memory, which mpu401_stream will
    // do for you.
    struct mpu401_streambuf final : realtime_streambuf
    {
        mpu401_streambuf(const mpu401_config& c);
        virtual ~mpu401_streambuf();

        virtual void put_realtime(char_type) override;

    protected:
        virtual std::streamsize showmanyc() override;
        virtual int_type underflow() override;
        virtual int_type pbackfail(int_type = traits_type::eof()) override;
        virtual int_type overflow(int_type = traits_type::eof()) override;
        virtual int sync() override;

    private:
        template<typename T>
        using allocator = default_constructing_allocator_adaptor<std::pmr::polymorphic_allocator<T>>;

        using rx_queue = dynamic_circular_queue<char_type, queue_sync::producer_irq, allocator<char_type>>;
        using tx_queue = dynamic_circular_queue<char_type, queue_sync::consumer_irq, allocator<char_type>>;
        using error_queue = std::deque<rx_queue::iterator, allocator<rx_queue::iterator>>;

        void do_setp(tx_queue::iterator) noexcept;
        void get_one() noexcept;
        std::uint8_t try_get() noexcept;
        void do_sync() noexcept;
        void do_sync(std::uint8_t) noexcept;
        void irq_handler() noexcept;

        const port_num base;
        rx_queue rx_buf;
        tx_queue tx_buf;
        error_queue errors;
        rx_queue::iterator* first_error;
        tx_queue::atomic_const_iterator tx_stop;
        const std::size_t putback_reserve;
        dpmi::irq_handler irq;
    };

    struct mpu401_stream : std::iostream
    {
        mpu401_stream(mpu401_config cfg)
            : std::iostream { }
            , streambuf { cfg.irq ? new (locked) mpu401_streambuf { cfg } : new mpu401_streambuf { cfg } }
        {
            init(streambuf.get());
        }

        mpu401_streambuf* rdbuf() const noexcept
        {
            return streambuf.get();
        }

        mpu401_stream(const mpu401_stream&) = delete;

    private:
        std::unique_ptr<mpu401_streambuf> streambuf;
    };
}
