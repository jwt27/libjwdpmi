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

            vbe() { init(); }

            const vbe_info& get_vbe_info();
            const std::list<vbe_mode_info>& get_modes() { get_vbe_info(); return modes; }
            virtual void set_mode(vbe_mode m, const crtc_info* crtc = nullptr) override;

        protected:
            virtual void init();
            void check_error(split_uint16_t ax, const char* function_name);
            void populate_mode_list(dpmi::far_ptr16 list_ptr);

            vbe_info info;
            std::list<vbe_mode_info> modes { };
        };

        struct vbe2 : public vbe
        {

        protected:
            virtual void init() override;
        };

        struct vbe3 : public vbe2
        {
            virtual void set_mode(vbe_mode m, const crtc_info* crtc = nullptr) override;

        protected:
            virtual void init() override;
        };
    }
}
