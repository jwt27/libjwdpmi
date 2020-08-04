/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <array>
#include <memory>
#include <atomic>
#include <deque>
#include <xmmintrin.h>
#include <jw/dpmi/irq_check.h>
#include <jw/dpmi/lock.h>
#include <jw/dpmi/alloc.h>
#include <jw/dpmi/irq.h>
#include <jw/alloc.h>
#include <jw/common.h>
#include <jw/split_stdint.h>
#include <jw/dpmi/ring0.h>
#include <../jwdpmi_config.h>

namespace jw
{
    namespace dpmi
    {
        union alignas(0x08) long_fpu_register
        {
            byte value[0x10];
            long double value_ld;
            double value_d;
            float value_f;
            split_int64_t mmx;
            __m64 m64;
        };
        static_assert(sizeof(long_fpu_register) == 0x10);

        union [[gnu::packed]] short_fpu_register
        {
            byte value[10];
            double value_d;
            float value_f;
            split_int64_t mmx;
            __m64 m64;
        };
        static_assert(sizeof(short_fpu_register) == 10);

        union alignas(0x10) sse_register
        {
            std::array<float, 4> value;
            __m128 m128;
        };
        static_assert(sizeof(sse_register) == 0x10);

        struct alignas(8) fsave_data
        {
            std::uint16_t fctrl;
            unsigned : 16;
            std::uint16_t fstat;
            unsigned : 16;
            std::uint16_t ftag;
            unsigned : 16;
            std::uintptr_t fioff;
            selector fiseg;
            std::uint16_t fop;
            std::uintptr_t fooff;
            selector foseg;
            unsigned : 16;
            short_fpu_register st[8];

            void save() noexcept { asm("fsave %0;"::"m" (*this)); }
            void restore() noexcept { asm("frstor %0;"::"m" (*this)); }
        };
        static_assert(sizeof(fsave_data) >= 108); // it's 112 for some reason

        struct alignas(0x10) fxsave_data
        {
            std::uint16_t fctrl;
            std::uint16_t fstat;
            std::uint8_t ftag;
            unsigned : 8;
            std::uint16_t fop;
            std::uintptr_t fioff;
            selector fiseg;
            unsigned : 16;
            std::uintptr_t fooff;
            selector foseg;
            unsigned : 16;
            std::uint32_t mxcsr;
            std::uint32_t mxcsr_mask;
            long_fpu_register st[8];
            sse_register xmm[8];
            unsigned reserved[44];
            byte unused[48];

            void save() noexcept { asm("fxsave %0;"::"m" (*this)); }
            void restore() noexcept { asm("fxrstor %0;"::"m" (*this)); }
        };
        static_assert(sizeof(fxsave_data) == 512);

#       ifdef HAVE__SSE__
        using fpu_context = fxsave_data;
#       else
        using fpu_context = fsave_data;
#       endif

        namespace detail
        {
            class fpu_context_switcher_t : class_lock<fpu_context_switcher_t>
            {
                locked_pool_allocator<false, fpu_context> alloc { config::interrupt_fpu_context_pool };
                std::deque<fpu_context*, locked_pool_allocator<false, fpu_context*>> contexts { alloc };

                bool context_switch_successful { false };
                bool init { false };
                bool cr0_em { false };

                void set_fpu_emulation(bool em, bool mp = true);
                bool get_fpu_emulation();

            public:
                fpu_context_switcher_t();
                ~fpu_context_switcher_t();

                INTERRUPT bool enter(std::uint32_t) noexcept;
                INTERRUPT void leave() noexcept;

                fpu_context* get_last_context()
                {
                    asm volatile ("fnop;fwait;":::"memory");   // force a context switch
                    for (auto i = contexts.rbegin(); i != contexts.rend(); ++i)
                        if (*i != nullptr) return *i;
                    return nullptr;
                }
            };
            inline std::unique_ptr<fpu_context_switcher_t> fpu_context_switcher;
        }
    }
}
