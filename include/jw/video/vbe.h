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

        protected:
            vbe_info info;
            std::list<vbe_mode_info> modes { };
        };

        struct vbe2 : public vbe
        {
            virtual const vbe_info& get_vbe_info();
        };

        struct vbe3 : public vbe2
        {
            
        };
    }
}
