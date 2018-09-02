/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
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
        struct vbe : public vga
        {
            struct error : public std::runtime_error { using runtime_error::runtime_error; };
            struct not_supported : public error { using error::error; };
            struct failed : public error { using error::error; };
            struct not_supported_in_current_hardware : public error { using error::error; };
            struct invalid_in_current_video_mode : public error { using error::error; };

            using vga::set_palette;
            virtual void init();
            const vbe_info& get_vbe_info();
            const std::map<std::uint_fast16_t,vbe_mode_info>& get_modes() { get_vbe_info(); return modes; }
            virtual void set_mode(vbe_mode m, const crtc_info* crtc = nullptr) override;
            virtual std::tuple<std::size_t, std::size_t, std::size_t> set_scanline_length(std::size_t width, bool width_in_pixels = true);
            virtual std::tuple<std::size_t, std::size_t, std::size_t> get_scanline_length();
            virtual std::tuple<std::size_t, std::size_t, std::size_t> get_max_scanline_length();
            virtual void set_display_start(vector2i pos, bool wait_for_vsync = false);
            virtual vector2i get_display_start();
            virtual void schedule_display_start(vector2i pos);
            virtual bool get_scheduled_display_start_status();
            virtual std::uint8_t set_palette_format(std::uint8_t bits_per_channel);
            virtual std::uint8_t get_palette_format();

            std::size_t get_bits_per_pixel()
            {
                std::size_t pixels, bytes;
                std::tie(pixels, bytes, std::ignore) = get_scanline_length();
                return bytes * 8 / pixels;
            }

            std::size_t get_lfb_size_in_pixels()
            {
                std::size_t line_size;
                std::tie(line_size, std::ignore, std::ignore) = get_scanline_length();
                return line_size * mode_info->resolution_y * mode_info->linear_num_image_pages;
            }

        protected:
            void check_error(split_uint16_t ax, const char* function_name);
            void populate_mode_list(dpmi::far_ptr16 list_ptr);

            vbe_info info;
            std::map<std::uint_fast16_t, vbe_mode_info> modes { };
            vbe_mode mode;
            vbe_mode_info* mode_info { nullptr };
        };

        struct vbe2 : public vbe
        {
            using vbe::set_palette;
            virtual void init() override;
            virtual void set_display_start(vector2i pos, bool wait_for_vsync = false) override;
            virtual void set_palette(const px32n* begin, const px32n* end, std::size_t first = 0, bool wait_for_vsync = false) override;
            virtual std::vector<px32n> get_palette() override;
        };

        struct vbe3 : public vbe2
        {
            using vbe2::set_palette;
            virtual void init() override;
            virtual void set_mode(vbe_mode m, const crtc_info* crtc = nullptr) override;
            //virtual vbe_mode get_current_mode()
            //virtual save_state()
            //virtual restore_state()
            //virtual std::uint32_t set_window()
            //virtual std::uint32_t get_window()
            virtual std::tuple<std::size_t, std::size_t, std::size_t> set_scanline_length(std::size_t width, bool width_in_pixels = true) override;
            virtual std::tuple<std::size_t, std::size_t, std::size_t> get_max_scanline_length() override;
            virtual void set_display_start(vector2i pos, bool wait_for_vsync = false) override;
            virtual void schedule_display_start(vector2i pos) override;
            //virtual void schedule_stereo_display_start(bool wait_for_vsync = false)
            virtual bool get_scheduled_display_start_status() override;
            //virtual void enable_stereo()
            //virtual void disable_stereo()
            virtual std::uint8_t set_palette_format(std::uint8_t bits_per_channel) override;
            virtual void set_palette(const px32n* begin, const px32n* end, std::size_t first = 0, bool wait_for_vsync = false) override;
            virtual std::uint32_t get_closest_pixel_clock(std::uint32_t desired_clock, std::uint16_t mode_num);
        };

        inline std::unique_ptr<vbe> get_vbe_interface()
        {
            try
            {
                auto v = std::make_unique<vbe3>();
                v->init();
                return std::move(v);
            }
            catch (const vbe::not_supported&) { }
            try
            {
                auto v = std::make_unique<vbe2>();
                v->init();
                return std::move(v);
            }
            catch (const vbe::not_supported&) { }

            auto v = std::make_unique<vbe>();
            v->init();
            return std::move(v);
        }
    }
}
