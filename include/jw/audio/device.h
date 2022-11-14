/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/audio/sample.h>
#include <jw/function.h>
#include <memory>

namespace jw::audio
{
    template<sample_type T>
    struct buffer : std::span<T>
    {
        constexpr buffer(T* begin, std::size_t size, std::size_t ch) noexcept
            : std::span<T> { begin, size }, channels { ch } { }

        constexpr buffer() noexcept = default;
        constexpr buffer(buffer&&) noexcept = default;
        constexpr buffer(const buffer&) noexcept = default;
        constexpr buffer& operator=(buffer&&) noexcept = default;
        constexpr buffer& operator=(const buffer&) noexcept = default;

        std::size_t channels;
    };

    template<sample_type T>
    struct io_buffer
    {
        const buffer<const T> in;
        const buffer<T> out;
    };

    struct start_parameters
    {
        // Desired sample rate.
        unsigned sample_rate;

        struct
        {
            // DMA buffer size in frames.
            std::size_t buffer_size;

            // Numver of audio channels.
            std::size_t channels;
        } in, out;
    };

    // Universal interface for all DMA-driven PCM audio devices.
    template<sample_type T>
    struct device final
    {
        using buffer_type = io_buffer<T>;
        using callback_type = void(const buffer_type&);

        struct driver
        {
            virtual ~driver() = default;
            virtual void start(const start_parameters&) = 0;
            virtual void stop() = 0;
            virtual buffer_type buffer() = 0;

            function<callback_type, 4> callback;
        };

        device(driver* impl) : drv { impl } { }
        ~device() { if (drv) drv->stop(); }

        // Begin playback and/or recording.  Specify nullptr as callback to
        // enable polling mode.
        template<typename F>
        void start(start_parameters params, F&& callback)
        {
            drv->callback = std::forward<F>(callback);
            drv->start(params);
        }

        void stop() { drv->stop(); }

        template<typename F>
        void process(F&& callback)
        {
            std::forward<F>(callback)(drv->buffer());
        }

    private:
        std::unique_ptr<driver> drv;
    };

    // Universal interface for PIO audio devices.
    template<sample_type T, std::size_t channels>
    struct pio_device
    {
        virtual std::array<T, channels> in() = 0;
        virtual void out(std::array<T, channels>) = 0;

        virtual ~pio_device() = default;
    };
}
