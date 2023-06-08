#/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
#/*    Copyright (C) 2017 - 2023 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <map>
#include <vector>
#include <jw/video/vga.h>
#include <jw/video/vbe_types.h>
#include <jw/video/pixel.h>
#include <jw/vector.h>

namespace jw::video
{
    struct vbe;

    // Initialize VBE and return a pointer to the interface, or nullptr if
    // initialization failed.  The pointer may be dynamic_cast in order to
    // access VBE3-specific features.
    vbe* vbe_interface();

    struct vbe : vga
    {
        struct error : std::runtime_error { using runtime_error::runtime_error; };
        struct not_supported : error { using error::error; };
        struct failed : error { using error::error; };
        struct not_supported_in_current_hardware : error { using error::error; };
        struct invalid_in_current_video_mode : error { using error::error; };

        const vbe_info& info();
        const std::map<std::uint_fast16_t, vbe_mode_info>& modes();
        virtual void set_mode(vbe_mode m, const crtc_info* crtc = nullptr) override;
        scanline_info scanline_length(std::size_t width, bool width_in_pixels = true);
        scanline_info scanline_length();
        scanline_info max_scanline_length();
        virtual void display_start(vector2i pos, bool wait_for_vsync = false);
        virtual vector2i display_start();
        virtual void schedule_display_start(vector2i pos);
        virtual bool scheduled_display_start_status();
        virtual std::uint8_t palette_format(std::uint8_t bits_per_channel);
        virtual std::uint8_t palette_format();
        std::size_t lfb_size_in_pixels();
        std::size_t bits_per_pixel();

    protected:
        friend vbe* vbe_interface();
        vbe() noexcept = default;
        virtual ~vbe() = default;
        virtual bool init();
    };

    struct vbe2 : vbe
    {
        virtual void display_start(vector2i pos, bool wait_for_vsync = false) override;
        virtual void palette(std::span<const px32n>, std::size_t first = 0, bool wait_for_vsync = false) override;
        virtual std::array<px32n, 256> palette() override;

    protected:
        friend vbe* vbe_interface();
        vbe2() noexcept = default;
        virtual ~vbe2() = default;
        virtual bool init() override;
    };

    struct vbe3 final : vbe2
    {
        virtual void set_mode(vbe_mode m, const crtc_info* crtc = nullptr) override;
        virtual void display_start(vector2i pos, bool wait_for_vsync = false) override;
        virtual void schedule_display_start(vector2i pos) override;
        virtual bool scheduled_display_start_status() override;
        virtual std::uint8_t palette_format(std::uint8_t bits_per_channel) override;
        virtual void palette(std::span<const px32n>, std::size_t first = 0, bool wait_for_vsync = false) override;
        std::uint32_t closest_pixel_clock(std::uint32_t desired_clock, std::uint16_t mode_num);

        // Not (yet) implemented:
        //void schedule_stereo_display_start(bool wait_for_vsync = false)
        //void enable_stereo()
        //void disable_stereo()
        //vbe_mode get_current_mode()
        //void save_state()
        //void restore_state()
        //std::uint32_t set_window()
        //std::uint32_t get_window()

    protected:
        friend vbe* vbe_interface();
        vbe3() noexcept = default;
        virtual ~vbe3() = default;
        virtual bool init() override;
    };
}
