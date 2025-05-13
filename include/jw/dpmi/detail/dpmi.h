/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2025 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#include <jw/dpmi/dpmi.h>

namespace jw::dpmi
{
    inline version::version() noexcept
    {
        split_uint16_t ax { 0x0400 }, dx;
        asm
        (
            "int 0x31;"
            : "+a" (ax)
            , "=b" (flags)
            , "=c" (cpu_type)
            , "=d" (dx)
            :
            : "cc"
        );
        major = ax.hi;
        minor = ax.lo;
        pic_master_base = dx.hi;
        pic_slave_base = dx.lo;
    }

    inline std::optional<capabilities> capabilities::get() noexcept
    {
        force_frame_pointer();
        capabilities cap;
        bool c = false;
        asm
        (
            "push es;"
            "push ds;"
            "pop es;"
            "int 0x31;"
            "pop es;"
            : "=a" (cap.flags)
            , "=@ccc"(c)
            : "a" (std::uint16_t { 0x0401 })
            , "D" (&cap.vendor_info)
            : "cc", "cx", "dx", "memory"
        );
        if (not c) [[likely]]
            return { std::move(cap) };
        else
            return std::nullopt;
    }
}
