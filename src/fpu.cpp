/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <jw/main.h>
#include <jw/dpmi/fpu.h>
#include <jw/dpmi/cpu_exception.h>
#include <jw/dpmi/irq_handler.h>
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
                setup_exception_throwers();

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
                if (not init) [[unlikely]] return false;

                auto try_context_switch = [this, exc]
                {
                    if (exc != exception_num::device_not_available and exc != exception_num::invalid_opcode) [[likely]] return false;
                    if (not get_fpu_emulation()) [[unlikely]] return false;

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
                if (not init) [[unlikely]] return;
                if (context_switch_successful)
                {
                    context_switch_successful = false;
                    return;
                }

                if (contexts.back() != nullptr) alloc.deallocate(contexts.back(), 1);
                contexts.pop_back();

                set_fpu_emulation(contexts.back() != nullptr or (contexts.size() > 1 and get_fpu_emulation()));
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
                cr0_em = em;
            }

            bool fpu_context_switcher_t::get_fpu_emulation()
            {
                // int 0x31,0e00 is no longer called here, primarily because cwsdpmi doesn't implement this.
                // even though the DPMI documentation claims that "all known DPMI 0.9 hosts support this function"...
                // we don't need any other bits besides this one anyway, so it ends up being faster too.
                return cr0_em;
            }
        }
    }
}
