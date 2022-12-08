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
    struct rs232_streambuf : std::streambuf
    {
        rs232_streambuf(const rs232_config&);
        virtual ~rs232_streambuf();

        rs232_streambuf() = delete;
        rs232_streambuf(const rs232_streambuf&) = delete;
        rs232_streambuf(rs232_streambuf&&) = delete;

        std::string_view view() const
        {
            const char* const p = gptr();
            std::size_t size = egptr() - p;
            return { p, size };
        }

    protected:
        virtual int sync() override;
        virtual std::streamsize xsgetn(char_type*, std::streamsize) override;
        virtual int_type underflow() override;
        virtual std::streamsize xsputn(const char_type*, std::streamsize) override;
        virtual int_type overflow(int_type = traits_type::eof()) override;

    private:
        void check_irq_exception();
        void set_rts() noexcept;
        void get(bool entire_fifo = false);
        void put();
        char_type get_one();
        bool put_one(char_type c);
        void irq_handler();

        const rs232_config cfg;
        dpmi::irq_handler irq;
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
