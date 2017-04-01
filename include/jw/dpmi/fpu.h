/******************************* libjwdpmi **********************************
Copyright (C) 2016-2017  J.W. Jagersma

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once
#include <array>
#include <memory>
#include <atomic>
#include <deque>
#include <jw/dpmi/irq_check.h>
#include <jw/dpmi/lock.h>
#include <jw/dpmi/alloc.h>
#include <jw/dpmi/irq.h>
#include <jw/alloc.h>
#include <jw/common.h>
#include <../jwdpmi_config.h>

// TODO: find some way to completely avoid generating fpu instructions
//#define NO_FPU_ATTR [[gnu::flatten, gnu::optimize("no-tree-vectorize"), gnu::target("no-mmx", "no-sse")]]

namespace jw
{
    namespace dpmi
    {
        class alignas(0x10) fpu_context
        {
        #ifdef __SSE__
            std::array<byte, 512+0x10> context;
        #else
            std::array<byte, 108+0x10> context;
        #endif
        public:
            void save() noexcept
            {
                auto ptr = context.data();
                asm("and %0, -0x10;"
                    "add %0, 0x10;"
            #ifdef __SSE__
                    "fxsave [%0];"
            #else
                    "fnsave [%0];"
            #endif
                    ::"r"(ptr));
            }
            void restore() noexcept
            {
                auto ptr = context.data();
                asm("and %0, -0x10;"
                    "add %0, 0x10;"
            #ifdef __SSE__
                    "fxrstor [%0];"
            #else
                    "frstor [%0];"
            #endif
                    ::"r"(ptr));
            }
        };

        namespace detail
        {
            bool test_cr0_access();
            bool test_cr4_access();

            struct [[gnu::packed]] cr0_t
            {
                bool protected_mode : 1;
                bool monitor_fpu : 1;
                bool fpu_emulation : 1;
                bool task_switched : 1;
                bool fpu_387 : 1;
                bool native_exceptions : 1;
                unsigned : 10;
                bool write_protect : 1;
                unsigned : 1;
                bool alignment_check : 1;
                unsigned : 10;
                bool disable_write_through : 1;
                bool disable_cache : 1;
                bool enable_paging : 1;

                cr0_t()
                {
                    bool cr0_access = test_cr0_access();
                    if (!cr0_access) return;
                    asm volatile(
                        "test %b1, %b1;"
                        "jz skip%=;"
                        "mov %0, cr0;"
                        "skip%=:"
                        : "=r" (*this)
                        : "q" (cr0_access));
                }
                void set()
                {
                    bool cr0_access = test_cr0_access();
                    if (!cr0_access) return;
                    asm volatile(
                        "test %b1, %b1;"
                        "jz skip%=;"
                        "mov cr0, %0;"
                        "skip%=:"
                        :: "r" (*this)
                        , "q" (cr0_access));
                }
            };

            class fpu_context_switcher_t : class_lock<fpu_context_switcher_t>
            {
                locked_pool_allocator<fpu_context> alloc { config::interrupt_fpu_context_pool };
                std::deque<fpu_context*, locked_pool_allocator<>> contexts { alloc };
                
                fpu_context default_irq_context;
                bool lazy_switching { false };
                bool init { false };
                std::uint32_t last_restored { 0 };
                
                INTERRUPT void switch_context()
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
                    else contexts.back()->restore();
                    last_restored = contexts.size() - 1;
                }

            public:
                fpu_context_switcher_t();
                ~fpu_context_switcher_t();

                INTERRUPT void enter() noexcept
                {
                    if (!init) return;
                    contexts.push_back(nullptr);
                    if (!lazy_switching) switch_context();
                    cr0_t cr0 { };
                    cr0.task_switched = true;
                    cr0.set();
                }

                INTERRUPT void leave() noexcept
                {
                    if (!init) return;
                    if (contexts.back() != nullptr) alloc.deallocate(contexts.back(), 1);
                    contexts.pop_back();
                    if (!lazy_switching) switch_context();
                    cr0_t cr0 { };
                    cr0.task_switched = (last_restored != contexts.size() - 1);
                    cr0.set();
                }

                fpu_context* get_last_context()
                {
                    if (contexts.back() == nullptr) switch_context();
                    return contexts[last_restored];
                }
            } extern fpu_context_switcher;
        }
    }
}