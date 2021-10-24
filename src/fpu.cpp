/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */

#include <optional>
#include <jw/dpmi/detail/interrupt_id.h>
#include <jw/dpmi/cpu_exception.h>

namespace jw::dpmi::detail
{
    std::optional<exception_handler> exc07_handler, exc06_handler;
    constinit bool cr0_em { false };

    static void set_fpu_emulation(bool em, bool mp = true)
    {
        dpmi_error_code error;
        bool c;
        asm volatile(
            "int 0x31;"
            : "=@ccc" (c)
            , "=a" (error)
            : "a" (0x0E01)
            , "b" (mp | (em << 1))
            : "cc");
        if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
        cr0_em = em;
    }

    void interrupt_id::fpu_enter()
    {
        if (type == interrupt_type::exception and cr0_em
            and (num == exception_num::device_not_available
                 or num == exception_num::invalid_opcode)) [[unlikely]]
        {
            set_fpu_emulation(false);
            if (next->has_fpu_context)
            {
                next->fpu->restore();
                next->has_fpu_context = false;
            }
            else if (not current_fpu->has_fpu_context)
            {
                current_fpu->fpu->save();
                current_fpu->has_fpu_context = true;
            }
            current_fpu = next;
            fpu_context_switched = true;
        }
        else if (not cr0_em) set_fpu_emulation(true);
    }

    void interrupt_id::fpu_leave()
    {
        if (fpu_context_switched) return;
        if (current_fpu == this) current_fpu = next_fpu;
        if (not cr0_em and next->has_fpu_context) set_fpu_emulation(true);
        else if (cr0_em and next->next == nullptr) set_fpu_emulation(false);
    }

    void interrupt_id::setup_fpu()
    {
        asm
        (R"(
            fnclex
            fninit
            sub esp, 4
            fnstcw [esp]
            or word ptr [esp], 0x00BF   # mask all exceptions
            fldcw [esp]
            add esp, 4
        )");
#       ifdef HAVE__SSE__
        asm
        (R"(
            sub esp, 4
            stmxcsr [esp]
            or dword ptr [esp], 0x00001F80
            ldmxcsr [esp]
            add esp, 4
        )");
#       endif

        set_fpu_emulation(false, true);

        exc06_handler.emplace(exception_num::invalid_opcode, [](cpu_registers*, exception_frame*, bool) { return false; });
        exc07_handler.emplace(exception_num::device_not_available, [](cpu_registers*, exception_frame*, bool) { return false; });
    }
}
