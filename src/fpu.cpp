/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma GCC target("no-sse", "fpmath=387")
#pragma GCC optimize("no-fast-math", "no-tree-vectorize")

#include <jw/dpmi/fpu.h>
#include <jw/dpmi/cpu_exception.h>
#include <jw/dpmi/irq.h>
#include <jw/dpmi/irq_mask.h>

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
                set_fpu_emulation(false);
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
                contexts.push_back(nullptr);

                reinstall_exception_handlers();

                init = true;
            }

            fpu_context_switcher_t::~fpu_context_switcher_t()
            {
                init = false;
            }

            void fpu_context_switcher_t::reinstall_exception_handlers()
            {
                set_fpu_emulation(false, true);
                if (test_cr0_access())
                {
                    interrupt_mask no_irqs { };
                    {
                        cr0_t cr0 { };
                        cr0.task_switched = true;
                        cr0.set();
                    }
                    cr0_t cr0 { };
                    use_ts_bit = cr0.task_switched;
                    cr0.task_switched = false;
                    cr0.set();
                }

                if (not use_ts_bit)
                {
                    exc06_handler = std::make_unique<exception_handler>(exception_num::invalid_opcode, [this](cpu_registers*, exception_frame*, bool) INTERRUPT
                    {
                        if (not get_fpu_emulation().em) return false;
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
                        if (not cr0.task_switched) return false;
                        cr0.task_switched = false;
                        cr0.set();
                    }
                    else
                    {
                        if (not get_fpu_emulation().em) return false;
                        set_fpu_emulation(false);
                    }
                    switch_context();
                    return true;
                });
            }

            void fpu_context_switcher_t::switch_context()
            {
                if (contexts.back() == nullptr)
                {
                    if (last_restored < contexts.size() - 1)
                    {
                        if (contexts[last_restored] == nullptr) contexts[last_restored] = alloc.allocate(1);
                        contexts[last_restored]->save();
                    }
                    default_irq_context.restore();
                }
                else
                {
                    contexts.back()->restore();
                    alloc.deallocate(contexts.back(), 1);
                    contexts.back() = nullptr;
                }
                last_restored = contexts.size() - 1;
            }

            void fpu_context_switcher_t::enter() noexcept
            {
                if (__builtin_expect(not init, false)) return;
                contexts.push_back(nullptr);
                if (not use_ts_bit) set_fpu_emulation(true);
                else
                {
                    cr0_t cr0 { };
                    cr0.task_switched = true;
                    cr0.set();
                }
            }

            void fpu_context_switcher_t::leave() noexcept
            {
                if (__builtin_expect(not init, false)) return;
                if (contexts.back() != nullptr) alloc.deallocate(contexts.back(), 1);
                contexts.pop_back();
                bool switch_required = last_restored != (contexts.size() - 1);
                if (not use_ts_bit) set_fpu_emulation(switch_required);
                else
                {
                    cr0_t cr0 { };
                    cr0.task_switched = switch_required;
                    cr0.set();
                }
            }

            void fpu_context_switcher_t::set_fpu_emulation(bool em, bool mp)
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
            }

            fpu_context_switcher_t::fpu_emulation_status fpu_context_switcher_t::get_fpu_emulation()
            {
                fpu_emulation_status status;
                asm volatile(
                    "int 0x31;"
                    : "=a" (status)
                    : "a" (0x0E00)
                    : "cc");
                return status;
            }
        }
    }
}
