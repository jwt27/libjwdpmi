/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <jw/dpmi/fpu.h>
#include <jw/dpmi/cpu_exception.h>
#include <jw/dpmi/irq.h>

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            bool cr0_allowed { true };
            bool cr0_access_known { false };
            bool test_cr0_access()
            {
                if (__builtin_expect(!cr0_access_known, false))
                {
                    volatile bool test { true };
                    dpmi::exception_handler exc { 0x0d, [&test](auto*, exception_frame* frame, bool)
                    {
                        test = false;
                        frame->fault_address.offset += 3;
                        return true;
                    } };

                    asm volatile(
                        "mov eax, cr0;" // both instructions are 3 bytes
                        "mov cr0, eax;"
                        :::"eax");

                    cr0_allowed = test;
                    cr0_access_known = true;
                }
                return cr0_allowed;
            }

            bool cr4_allowed { true };
            bool cr4_access_known { false };
            bool test_cr4_access()
            {
                if (__builtin_expect(!cr4_access_known, false))
                {
                    volatile bool test { true };
                    dpmi::exception_handler exc { 0x0d, [&test](auto*, exception_frame* frame, bool)
                    {
                        test = false;
                        frame->fault_address.offset += 3;
                        return true;
                    } };

                    asm volatile(
                        "mov eax, cr4;"
                        "mov cr4, eax;"
                        :::"eax");

                    cr4_allowed = test;
                    cr4_access_known = true;
                }
                return cr4_allowed;
            }

            std::unique_ptr<exception_handler> exc07_handler, exc06_handler;

            fpu_context_switcher_t::fpu_context_switcher_t()
            {
                cr0_t cr0 { };
                cr0.native_exceptions = true;
                cr0.monitor_fpu = true;
                cr0.task_switched = false;
            #ifdef __SSE__
                asm("test %b0, %b0;"
                    "jz skip%=;"
                    "mov eax, cr4;"
                    "or eax, 0x600;" // enable SSE and SSE exceptions
                    "mov cr4,eax;"
                    "skip%=:"
                    ::"q"(test_cr4_access())
                    : "eax");
                cr0.fpu_emulation = false;
            #endif
                cr0.set();

                asm("fnclex;"
                    "fninit;"
                    "sub esp, 4;"
                    "fnstcw [esp];"
                    "or word ptr [esp], 0x00BF;"   // mask all exceptions
                    "fldcw [esp];"
            #ifdef __SSE__
                    "stmxcsr [esp];"
                    "or dword ptr [esp], 0x00001F80;"
                    "ldmxcsr [esp];"
            #endif
                    "add esp, 4;");
                default_irq_context.save();
                contexts.emplace_back(nullptr);

                set_fpu_emulation(false, true);
                if (test_cr0_access()) use_ts_bit = true;
                else
                {
                    use_ts_bit = false;
                    exc06_handler = std::make_unique<exception_handler>(exception_num::invalid_opcode, [this](cpu_registers*, exception_frame*, bool) INTERRUPT
                    {
                        if (!get_fpu_emulation().em) return false;
                        set_fpu_emulation(false);
                        switch_context();
                        return true;
                    });
                }

                exc07_handler = std::make_unique<exception_handler>(exception_num::device_not_available, [this](cpu_registers*, exception_frame*, bool) INTERRUPT
                {
                    if (__builtin_expect(use_ts_bit, true))
                    {
                        cr0_t cr0 { };
                        cr0.task_switched = false;
                        cr0.set();
                    }
                    else
                    {
                        set_fpu_emulation(false);
                    }
                    switch_context();
                    return true;
                });

                init = true;
            }

            fpu_context_switcher_t::~fpu_context_switcher_t()
            {
                init = false;
            }
        }
    }
}
