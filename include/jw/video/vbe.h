/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstring>
#include <list>
#include <jw/video/vga.h>
#include <jw/video/vbe_types.h>

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

            virtual void init();
            const vbe_info& get_vbe_info();
            const std::map<std::uint_fast16_t,vbe_mode_info>& get_modes() { get_vbe_info(); return modes; }
            virtual void set_mode(vbe_mode m, const crtc_info* crtc = nullptr) override;
            virtual std::tuple<std::uint32_t, std::uintptr_t, std::uint32_t> set_scanline_length(std::uint32_t width, bool width_in_pixels = true);
            virtual std::tuple<std::uint32_t, std::uintptr_t, std::uint32_t> get_scanline_length();
            virtual std::tuple<std::uint32_t, std::uintptr_t, std::uint32_t> get_max_scanline_length();
            virtual void set_display_start(std::uint32_t first_pixel, std::uint32_t first_scanline, bool wait_for_vsync = false);

        protected:
            void check_error(split_uint16_t ax, const char* function_name);
            void populate_mode_list(dpmi::far_ptr16 list_ptr);

            vbe_info info;
            std::map<std::uint_fast16_t, vbe_mode_info> modes { };
        };

        struct vbe2 : public vbe
        {
            virtual void init() override;
            virtual void set_display_start(std::uint32_t first_pixel, std::uint32_t first_scanline, bool wait_for_vsync = false) override;
        };

        struct vbe3 : public vbe2
        {
            virtual void init() override;
            virtual void set_mode(vbe_mode m, const crtc_info* crtc = nullptr) override;
            //virtual vbe_mode get_current_mode()
            //virtual save_state()
            //virtual restore_state()
            //virtual std::uint32_t set_window()
            //virtual std::uint32_t get_window()
            virtual std::tuple<std::uint32_t, std::uintptr_t, std::uint32_t> set_scanline_length(std::uint32_t width, bool width_in_pixels = true) override;
            virtual std::tuple<std::uint32_t, std::uintptr_t, std::uint32_t> get_max_scanline_length() override;
            virtual void set_display_start(std::uint32_t first_pixel, std::uint32_t first_scanline, bool wait_for_vsync = false) override;
            //virtual std::tuple<std::uint32_t, std::uint32_t> get_display_start()
            //virtual void schedule_display_start(bool wait_for_vsync = false)
            //virtual void schedule_stereo_display_start(bool wait_for_vsync = false)
            //virtual bool get_scheduled_display_start_status()
            //virtual void enable_stereo()
            //virtual void disable_stereo()
            //virtual std::uint8_t set_dac_palette_format()
            //virtual std::uint8_t get_dac_palette_format()
            //virtual set_palette_data(bool wait_for_vsync)
            //virtual get_palette_data()
            //virtual set_palette_data_secondary()
            //virtual get_palette_data_secondary()
            //virtual std::uint32_t get_pixel_clock()
            //virtual std::uint32_t set_pixel_clock(std::uint32_t)
        };

        inline std::unique_ptr<vbe> get_vbe_interface()
        {
            try
            {
                auto v = std::make_unique<vbe3>();
                v->init();
                return std::move(v);
            }
            catch (vbe::not_supported) { }
            try
            {
                auto v = std::make_unique<vbe2>();
                v->init();
                return std::move(v);
            }
            catch (vbe::not_supported) { }

            auto v = std::make_unique<vbe>();
            v->init();
            return std::move(v);
        }
    }
}
