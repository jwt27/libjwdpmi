/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */

#include <jw/dpmi/fpu.h>
#include <jw/dpmi/irq_mask.h>
#include <jw/detail/scheduler.h>
#include "jwdpmi_config.h"

namespace jw::dpmi
{
    static constinit bool cr0_em { false };
    static constinit std::array<detail::fpu_state, config::fpu_context_storage_size> context_storage { };
    static constinit detail::fpu_state* free { context_storage.begin() };
    static constinit detail::fpu_state* save { nullptr };

    static void set_cr0_em(bool em)
    {
        const std::uint8_t bl = (em << 1) | 1;

        dpmi_error_code error;
        bool c;
        asm volatile(
            "int 0x31;"
            : "=@ccc" (c)
            , "=a" (error)
            : "a" (0x0E01)
            , "b" (bl)
            : "cc");
        if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
        cr0_em = em;
    }

    static bool in_use(detail::fpu_state* p)
    {
        return (p->save_count | p->restore_count) != 0;
    }

    static void try_free(detail::fpu_state* p)
    {
        if (not in_use(p) and p < free) free = p;
    }

    detail::fpu_state** fpu_context::restore_ptr() noexcept
    {
        return &jw::detail::scheduler::current_thread()->restore;
    }

    fpu_context::fpu_context(init_tag)
        : state { nullptr }
        , restore { nullptr }
    {
        set_cr0_em(false);
        asm
        (R"(
            fnclex
            fninit
            sub esp, 4
            fnstcw [esp]
            or byte ptr [esp], 0xBF     # mask all exceptions
            fldcw [esp]
            add esp, 4
        )");
#       ifdef HAVE__SSE__
        asm
        (R"(
            sub esp, 4
            stmxcsr [esp]
            or word ptr [esp], 0x1F80
            ldmxcsr [esp]
            add esp, 4
        )");
#       endif
    }

    fpu_context::fpu_context()
        : restore { restore_ptr() }
    {
        if (*restore != nullptr)
        {
            // Pending restore, cancel it and use the same state.
            state = *restore;
            *restore = nullptr;
            --state->restore_count;
        }
        else if (save != nullptr)
        {
            // Pending save, use the same state.
            state = save;
        }
        else
        {
            // Allocate new state.
            if (free == context_storage.end()) throw std::runtime_error { "FPU context storage exhausted" };
            state = free++;
            state->saved = false;
            save = state;
            while (in_use(free) and free != context_storage.end()) ++free;
        }
        ++state->save_count;
        update_cr0();
    }

    fpu_context::~fpu_context()
    {
        if (state == nullptr) [[unlikely]] return;
        interrupt_mask no_irqs { };
        --state->save_count;
        if (state->saved)
        {
            if (*restore != nullptr)
            {
                // Cancel pending restore.
                --(*restore)->restore_count;
                try_free(*restore);
            }
            // Schedule restore.
            *restore = state;
            ++state->restore_count;
        }
        else try_free(state);
        update_cr0();
    }

    fpu_registers* fpu_context::get()
    {
        if (save == state) // force a context switch
            asm volatile ("fnop; fwait" : : : "memory");
        return &state->regs;
    }

    void fpu_context::update_cr0()
    {
        const bool do_save = save != nullptr;
        const bool do_restore = *restore_ptr() != nullptr;
        const bool new_em = do_save | do_restore;

        if (new_em == cr0_em) return;
        set_cr0_em(new_em);
    }

    bool fpu_context::try_context_switch() noexcept
    {
        if (not cr0_em) return false;
        set_cr0_em(false);

        if (save != nullptr)
        {
            save->regs.save();
            save->saved = true;
            save = nullptr;
        }

        auto** const p = restore_ptr();
        if (*p != nullptr)
        {
            (*p)->regs.restore();
            --(*p)->restore_count;
            try_free(*p);
            *p = nullptr;
        }

        return true;
    }
}
