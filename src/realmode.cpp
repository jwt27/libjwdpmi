/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <jw/dpmi/realmode.h>

namespace jw
{
    namespace dpmi
    {
        void realmode_callback_base::alloc(void* func)
        {
            dpmi::dpmi_error_code error;
            bool c;
            asm volatile(
                "push es;"
                "push ds;"
                "pop es;"
                "int 0x31;"
                "pop es;"
                : "=@ccc" (c)
                , "=a" (error)
                , "=c" (ptr.segment)
                , "=d" (ptr.offset)
                : "a" (0x0303)
                , "S" (func)
                , "D" (&reg));
            if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
        }

        void realmode_callback_base::free()
        {
            dpmi::dpmi_error_code error;
            bool c;
            asm volatile(
                "push es;"
                "push ds;"
                "pop es;"
                "int 0x31;"
                "pop es;"
                : "=@ccc" (c)
                , "=a" (error)
                : "a" (0x0304)
                , "c" (ptr.segment)
                , "d" (ptr.offset));
            if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
        }
    }
}
