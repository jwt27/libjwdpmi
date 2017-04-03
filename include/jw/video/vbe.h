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
            virtual const vbe_info& get_vbe_info();
            const std::list<vbe_mode_info>& get_modes() { get_vbe_info(); return modes; }

        protected:
            void check_error(split_uint16_t ax, auto function_name);
            void populate_mode_list(dpmi::far_ptr16 list_ptr);

            vbe_info info;
            std::list<vbe_mode_info> modes { };
        };

        struct vbe2 : public vbe
        {
            virtual const vbe_info& get_vbe_info() override;
        };

        struct vbe3 : public vbe2
        {
            
        };
    }
}
