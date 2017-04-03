/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/dpmi/realmode.h>
#include <jw/video/vbe_types.h>

namespace jw
{
    namespace video
    {
        struct bios
        {
            virtual void set_mode(const vbe_mode& m)
            {
                dpmi::rm_registers reg { };
                reg.ah = 0x00;
                reg.al = m.mode;
                reg.call_rm_interrupt(0x10);
            }

            virtual void set_cursor_shape();

            virtual void set_cursor_position();
            

            virtual ~bios() { }
        };

        struct vga : public bios
        {

        };
    }
}
