/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

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
            std::unique_ptr<exception_handler> exc07_handler, exc06_handler;

            fpu_context_switcher_t::fpu_context_switcher_t()
            {
                cr0_t cr0 { };
                cr0.native_exceptions = true;
                cr0.task_switched = false;
                cr0.set();

                if constexpr (sse)
                {
                    ring0_privilege r0 { };
                    std::uint32_t scratch;
                    asm volatile
                    (
                        "mov %0, cr4;"
                        "or %0, 0x600;" // enable SSE and SSE exceptions
                        "mov cr4, %0;"
                        : "=r" (scratch)
                    );
                }

                asm
                (
                    "fnclex;"
                    "fninit;"
                    "sub esp, 4;"
                    "fnstcw [esp];"
                    "or word ptr [esp], 0x00BF;"   // mask all exceptions
                    "fldcw [esp];"
                    "add esp, 4;"
                );

                if constexpr (sse)
                {
                    asm
                    (
                        "sub esp, 4;"
                        "stmxcsr [esp];"
                        "or dword ptr [esp], 0x00001F80;"
                        "ldmxcsr [esp];"
                        "add esp, 4;"
                    );
                }

                contexts.push_back(nullptr);
                set_fpu_emulation(false, true);

                auto dummy_exception_handler = [this] (auto e) { return std::make_unique<exception_handler>(e, [this](cpu_registers*, exception_frame*, bool) { return context_switch_successful; }); };
                exc06_handler = dummy_exception_handler(exception_num::invalid_opcode);
                exc07_handler = dummy_exception_handler(exception_num::device_not_available);

                init = true;
            }

            fpu_context_switcher_t::~fpu_context_switcher_t()
            {
                init = false;
            }

            bool fpu_context_switcher_t::enter(std::uint32_t exc) noexcept
            {
                if (__builtin_expect(not init, false)) return false;

                auto try_context_switch = [this, exc]
                {
                    if (exc != exception_num::device_not_available and exc != exception_num::invalid_opcode) return false;
                    if (not get_fpu_emulation().em) return false;

                    set_fpu_emulation(false);

                    if (contexts.back() == nullptr)
                    {
                        for (auto& i : contexts)
                        {
                            if (i == nullptr)
                            {
                                i = alloc.allocate(1);
                                i->save();
                                break;
                            }
                        }
                    }
                    else
                    {
                        contexts.back()->restore();
                        alloc.deallocate(contexts.back(), 1);
                        contexts.back() = nullptr;
                    }
                    return true;
                };

                context_switch_successful = try_context_switch();

                if (not context_switch_successful)
                {
                    contexts.push_back(nullptr);
                    set_fpu_emulation(true);
                }
                return context_switch_successful;
            }

            void fpu_context_switcher_t::leave() noexcept
            {
                if (__builtin_expect(not init, false)) return;
                if (context_switch_successful)
                {
                    context_switch_successful = false;
                    return;
                }

                if (contexts.back() != nullptr) alloc.deallocate(contexts.back(), 1);
                contexts.pop_back();

                set_fpu_emulation(contexts.back() != nullptr or (contexts.size() > 1 and get_fpu_emulation().em));
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
