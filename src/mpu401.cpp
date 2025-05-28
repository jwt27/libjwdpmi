/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2017 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#include <jw/io/mpu401.h>
#include <jw/chrono.h>
#include <jw/thread.h>
#include <set>

namespace jw::io
{
    struct [[gnu::packed]] mpu401_status
    {
        unsigned : 6;
        bool dont_send_data : 1;        // "Data Receive Ready"
        bool no_data_available : 1;     // "Data Set Ready"
    };

    static out_port<byte>         command_port(port_num base) { return { base + 1 }; }
    static in_port<mpu401_status> status_port(port_num base)  { return { base + 1 }; }
    static io_port<std::uint8_t>  data_port(port_num base)    { return { base + 0 }; }

    static std::pmr::memory_resource* memres(bool use_irq) noexcept
    {
        if (use_irq) return dpmi::global_locked_pool_resource();
        else return std::pmr::get_default_resource();
    }

    static std::set<port_num> ports_used { };

    mpu401_streambuf::mpu401_streambuf(const mpu401_config& cfg)
        : base { cfg.port }
        , rx_buf { cfg.receive_buffer_size, memres(cfg.irq.has_value()) }
        , tx_buf { cfg.transmit_buffer_size, memres(cfg.irq.has_value()) }
        , errors { memres(cfg.irq.has_value()) }
        , putback_reserve { cfg.putback_reserve }
        , irq { [this] { irq_handler(); }, dpmi::no_auto_eoi }
    {
        if (ports_used.contains(base)) throw std::runtime_error { "MPU-401 port already in use" };

        using namespace std::chrono_literals;

        auto fail = [] { throw device_not_found { "MPU-401 not detected" }; };

        auto* const rx = rx_buf.consumer();
        auto* const tx = tx_buf.producer();
        const auto rx_begin = rx->begin();
        const auto tx_begin = tx->fill();

        setg(&*rx_begin, &*rx_begin, &*rx_begin);
        do_setp(tx_begin);
        tx_stop = tx_begin;

        std::optional<dpmi::irq_mask> no_irq;

        if (cfg.irq) no_irq.emplace(*cfg.irq);

        // Wait until we're clear to send.
        auto timeout = this_thread::yield_while_for([this]
        {
            auto status = status_port(base).read();
            while (not status.no_data_available)
            {
                data_port(base).read();
                status = status_port(base).read();
            }
            return status.dont_send_data;
        }, 25ms);
        if (timeout) fail();

        // Reset.  This won't ACK if the MPU is in UART mode already.
        command_port(base).write(0xff);

        timeout = this_thread::yield_while_for([this, fail]
        {
            const auto status = status_port(base).read();
            // If we receive anything, it must be an ACK.
            if (not status.no_data_available and data_port(base).read() != 0xfe) fail();
            return status.dont_send_data;
        }, 25ms);
        if (timeout) fail();

        // Enable UART mode.
        command_port(base).write(0x3f);

        // Receive ACK.
        timeout = this_thread::yield_while_for([this] { return status_port(base).read().no_data_available; }, 50ms);
        if (timeout) fail();
        if (data_port(base).read() != 0xfe) fail();

        if (no_irq.has_value())
        {
            irq.set_irq(*cfg.irq);
            irq.enable();
            no_irq.reset();

            // Ensure IRQ line is deasserted.
            while (not status_port(base).read().no_data_available)
                data_port(base).read();
        }

        ports_used.insert(base);
    }

    mpu401_streambuf::~mpu401_streambuf()
    {
        sync();
        irq.disable();
        this_thread::yield_while([this]
        {
            auto status = status_port(base).read();
            while (not status.no_data_available)
            {
                data_port(base).read();
                status = status_port(base).read();
            }
            return status.dont_send_data;
        });
        command_port(base).write(0xff);
        ports_used.erase(base);
    }

    void mpu401_streambuf::put_realtime(char_type out)
    {
        std::optional<dpmi::interrupt_mask> no_irq;
        if (irq.is_enabled()) no_irq.emplace();

        while (std::bit_cast<mpu401_status>(try_get()).dont_send_data)
            this_thread::yield();

        data_port(base).write(out);
    }

    std::streamsize mpu401_streambuf::showmanyc()
    {
        auto* const rx = rx_buf.consumer();
        const auto pos = rx->iterator_from_pointer(gptr());
        auto end = rx->cend();

        if (const auto* const err = volatile_load(&first_error)) [[unlikely]]
        {
            if (pos == *err)
                return -1;
            end = min(end, *err);
        }
        return pos.distance_to(end);
    }

    mpu401_streambuf::int_type mpu401_streambuf::underflow()
    {
        auto* const rx = rx_buf.consumer();

    retry:
        const auto pos = rx->iterator_from_pointer(gptr());
        rx->pop_front_to(clamp_add(pos, -putback_reserve, rx->begin(), pos));
        auto* new_end = rx->contiguous_end(pos);

        if (auto* const err = volatile_load(&first_error)) [[unlikely]]
        {
            if (pos != *err)
            {
                if (pos.distance_to(*err) < pos.distance_to(rx->iterator_from_pointer(new_end)))
                    new_end = &**err;
            }
            else
            {
                {
                    dpmi::interrupt_mask no_irq { };
                    asm("" ::: "memory");
                    errors.pop_front();
                    if (not errors.empty())
                        first_error = &errors.front();
                    else
                        first_error = nullptr;
                }
                throw io::overflow { "MPU-401 receive buffer overflow" };
            }
        }

        if (new_end == &*pos)
        {
            if (irq.is_enabled()) this_thread::yield();
            else do_sync();
            goto retry;
        }

        setg(rx->contiguous_begin(pos), &*pos, new_end);
        return traits_type::to_int_type(*gptr());
    }

    mpu401_streambuf::int_type mpu401_streambuf::pbackfail(int_type c)
    {
        if (eback() < gptr())
        {
            gbump(-1);
            traits_type::assign(*gptr(), traits_type::to_char_type(c));
            return traits_type::not_eof(c);
        }

        auto* const rx = rx_buf.consumer();
        auto i = rx->iterator_from_pointer(gptr());
        if (rx->begin().distance_to(i) > 0)
        {
            --i;
            setg(rx->contiguous_begin(i), &*i, rx->contiguous_end(i));
            traits_type::assign(*gptr(), traits_type::to_char_type(c));
            return traits_type::not_eof(c);
        }
        return traits_type::eof();
    }

    mpu401_streambuf::int_type mpu401_streambuf::overflow(int_type c)
    {
        auto* const tx = tx_buf.producer();
        const auto pos = tx->iterator_from_pointer(pptr());
        tx_stop = pos;

        if (pos == tx->cend() and tx->full())
        {
            std::optional<dpmi::interrupt_mask> no_irq;
            if (irq.is_enabled()) no_irq.emplace();
            this_thread::yield_while([&]
            {
                do_sync();
                return tx->full();
            });
        }
        tx->fill();
        do_setp(pos);

        if (not traits_type::eq_int_type(c, traits_type::eof()))
        {
            traits_type::assign(*pptr(), traits_type::to_char_type(c));
            pbump(1);
        }

        return traits_type::not_eof(c);
    }

    int mpu401_streambuf::sync()
    {
        auto* const tx = tx_buf.producer();
        const auto pos = tx->iterator_from_pointer(pptr());
        tx_stop = pos;

        {
            std::optional<dpmi::interrupt_mask> no_irq;
            if (irq.is_enabled()) no_irq.emplace();
            this_thread::yield_while([&]
            {
                do_sync();
                return tx->begin() != pos;
            });
        }

        do_setp(pos);
        return 0;
    }

    inline void mpu401_streambuf::do_setp(tx_queue::iterator i) noexcept
    {
        // Split the TX buffer into smaller chunks, so tx_stop is updated more
        // frequently.
        auto* const tx = tx_buf.producer();
        auto* const p = &*i;
        setp(p, std::min(p + std::max((tx->max_size() + 1) / 8, tx_queue::size_type { 1u }), tx->contiguous_end(i)));
    }

    // Receive one byte, without checking status.
    inline void mpu401_streambuf::get_one() noexcept
    {
        auto* const rx = rx_buf.producer();
        if (rx->empty())
            t = clock::now();

        const auto ok = rx->try_push_back(data_port(base).read());
        if (not ok) [[unlikely]]
        {
            if (errors.empty() or errors.back() != rx->end())
            {
                errors.emplace_back(rx->end());
                first_error = &errors.front();
            }
        }
    }

    // Receive as many bytes as possible.  Returns last read status.
    inline std::uint8_t mpu401_streambuf::try_get() noexcept
    {
        auto status = status_port(base).read();
        while (not status.no_data_available) [[likely]]
        {
            get_one();
            status = status_port(base).read();
        }
        return std::bit_cast<std::uint8_t>(status);
    }

    inline void mpu401_streambuf::do_sync() noexcept
    {
        return do_sync(std::bit_cast<std::uint8_t>(status_port(base).read()));
    }

    inline void mpu401_streambuf::do_sync(std::uint8_t s) noexcept
    {
        auto* const tx = tx_buf.consumer();
        auto status = std::bit_cast<mpu401_status>(s);
        if (not status.no_data_available) [[likely]] goto get;
        if (not status.dont_send_data) [[likely]] goto put;
        return;

        do
        {
        get:
            while (not status.no_data_available)
            {
                get_one();
                status = status_port(base).read();
            }

        put:
            while (not status.dont_send_data and tx->begin() != tx_stop) [[likely]]
            {
                data_port(base).write(tx->front());
                tx->pop_front();
                status = status_port(base).read();
            }
        } while (not status.no_data_available);
    }

    inline void mpu401_streambuf::irq_handler() noexcept
    {
        auto status = status_port(base).read();
        if (status.no_data_available) [[unlikely]]
            return;

        do_sync(std::bit_cast<std::uint8_t>(status));

        dpmi::irq_handler::acknowledge();
    }
}
