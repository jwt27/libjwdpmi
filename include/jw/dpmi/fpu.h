/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <array>
#include <xmmintrin.h>
#include <jw/split_int.h>

namespace jw::dpmi::detail
{
    extern const bool use_fxsave;
}

namespace jw::dpmi
{
#   pragma GCC diagnostic push
#   pragma GCC diagnostic error "-Wpadded"
#   pragma GCC diagnostic ignored "-Wpacked-not-aligned"

    union alignas(8) long_fpu_register
    {
        byte value[16];
        long double value_ld;
        double value_d;
        float value_f;
        split_int64_t mmx;
        __m64 m64;
    };
    static_assert(sizeof(long_fpu_register) == 16);

    union [[gnu::packed]] short_fpu_register
    {
        byte value[10];
        double value_d;
        float value_f;
        split_int64_t mmx;
        __m64 m64;
    };
    static_assert(sizeof(short_fpu_register) == 10);

    union alignas(16) sse_register
    {
        std::array<float, 4> value;
        __m128 m128;
    };
    static_assert(sizeof(sse_register) == 16);

    struct alignas(4) fsave_data
    {
        std::uint16_t fctrl;
        unsigned : 16;
        std::uint16_t fstat;
        unsigned : 16;
        std::uint16_t ftag;
        unsigned : 16;
        std::uintptr_t fioff;
        std::uint16_t fiseg;
        std::uint16_t fop;
        std::uintptr_t fooff;
        std::uint16_t foseg;
        unsigned : 16;
        std::array<short_fpu_register, 8> st;

        void save() noexcept { asm volatile ("fsave %0" : "=m" (*this)); }
        void restore() noexcept { asm volatile ("frstor %0" :: "m" (*this)); }
    };
    static_assert(sizeof(fsave_data) == 108);

    struct alignas(0x10) fxsave_data
    {
        std::uint16_t fctrl;
        std::uint16_t fstat;
        std::uint8_t ftag;
        unsigned : 8;
        std::uint16_t fop;
        std::uintptr_t fioff;
        std::uint16_t fiseg;
        unsigned : 16;
        std::uintptr_t fooff;
        std::uint16_t foseg;
        unsigned : 16;
        std::uint32_t mxcsr;
        std::uint32_t mxcsr_mask;
        std::array<long_fpu_register, 8> st;
        std::array<sse_register, 8> xmm;
        std::array<std::byte, 0xb0> reserved;
        std::array<std::byte, 0x30> unused;

        void save() noexcept { asm volatile ("fxsave %0" : "=m" (*this)); }
        void restore() noexcept { asm volatile ("fxrstor %0" :: "m" (*this)); }
    };
    static_assert(sizeof(fxsave_data) == 512);

#   pragma GCC diagnostic pop

    enum class fpu_registers_type { fsave, fxsave };

    union fpu_registers
    {
        fsave_data fsave;
        fxsave_data fxsave;

        void save() noexcept
        {
            switch (type())
            {
            case fpu_registers_type::fsave: return fsave.save();
            case fpu_registers_type::fxsave: return fxsave.save();
            default: __builtin_unreachable();
            }
        }

        void restore() noexcept
        {
            switch (type())
            {
            case fpu_registers_type::fsave: return fsave.restore();
            case fpu_registers_type::fxsave: return fxsave.restore();
            default: __builtin_unreachable();
            }
        }

        [[gnu::const]] static fpu_registers_type type() noexcept
        {
#           ifdef HAVE__SSE__
            return fpu_registers_type::fxsave;
#           else
            if (detail::use_fxsave)
                return fpu_registers_type::fxsave;
            else
                return fpu_registers_type::fsave;
#           endif
        }
    };

    // Saves the FPU registers and restores them on destruction.
    struct fpu_context
    {
        fpu_context() { registers.save(); };
        ~fpu_context() { registers.restore(); }

        fpu_registers registers;
    };
}
