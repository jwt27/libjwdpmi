/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <map>
#include <vector>
#include <jw/video/vga.h>
#include <jw/video/vbe_types.h>
#include <jw/video/pixel.h>
#include <jw/vector.h>

namespace jw
{
    namespace video
    {
        struct vbe;

        vbe* get_vbe_interface();

        struct scanline_length
        {
            std::size_t pixels_per_scanline;
            std::size_t bytes_per_scanline;
            std::size_t max_scanlines;
        };

        struct vbe : public vga
        {
            struct error : public std::runtime_error { using runtime_error::runtime_error; };
            struct not_supported : public error { using error::error; };
            struct failed : public error { using error::error; };
            struct not_supported_in_current_hardware : public error { using error::error; };
            struct invalid_in_current_video_mode : public error { using error::error; };

            const vbe_info& get_vbe_info();
            const std::map<std::uint_fast16_t, vbe_mode_info>& get_modes();
            virtual void set_mode(vbe_mode m, const crtc_info* crtc = nullptr) override;
            virtual scanline_length set_scanline_length(std::size_t width, bool width_in_pixels = true);
            virtual scanline_length get_scanline_length();
            virtual scanline_length get_max_scanline_length();
            virtual void set_display_start(vector2i pos, bool wait_for_vsync = false);
            virtual vector2i get_display_start();
            virtual void schedule_display_start(vector2i pos);
            virtual bool get_scheduled_display_start_status();
            virtual std::uint8_t set_palette_format(std::uint8_t bits_per_channel);
            virtual std::uint8_t get_palette_format();
            std::size_t get_lfb_size_in_pixels();

            std::size_t get_bits_per_pixel()
            {
                auto r = get_scanline_length();
                return r.bytes_per_scanline * 8 / r.pixels_per_scanline;
            }

        protected:
            friend vbe* get_vbe_interface();
            virtual bool init();
        };

        struct vbe2 : public vbe
        {
            using vbe::set_palette;
            virtual void set_display_start(vector2i pos, bool wait_for_vsync = false) override;
            virtual void set_palette(std::span<const px32n>, std::size_t first = 0, bool wait_for_vsync = false) override;
            virtual std::array<px32n, 256> get_palette() override;

        protected:
            friend vbe* get_vbe_interface();
            virtual bool init() override;
        };

        struct vbe3 final : public vbe2
        {
            using vbe2::set_palette;
            virtual void set_mode(vbe_mode m, const crtc_info* crtc = nullptr) override;
            //virtual vbe_mode get_current_mode()
            //virtual save_state()
            //virtual restore_state()
            //virtual std::uint32_t set_window()
            //virtual std::uint32_t get_window()
            virtual scanline_length set_scanline_length(std::size_t width, bool width_in_pixels = true) override;
            virtual scanline_length get_max_scanline_length() override;
            virtual void set_display_start(vector2i pos, bool wait_for_vsync = false) override;
            virtual void schedule_display_start(vector2i pos) override;
            //virtual void schedule_stereo_display_start(bool wait_for_vsync = false)
            virtual bool get_scheduled_display_start_status() override;
            //virtual void enable_stereo()
            //virtual void disable_stereo()
            virtual std::uint8_t set_palette_format(std::uint8_t bits_per_channel) override;
            virtual void set_palette(std::span<const px32n>, std::size_t first = 0, bool wait_for_vsync = false) override;
            virtual std::uint32_t get_closest_pixel_clock(std::uint32_t desired_clock, std::uint16_t mode_num);

        protected:
            friend vbe* get_vbe_interface();
            virtual bool init() override;
        };
    }
}
