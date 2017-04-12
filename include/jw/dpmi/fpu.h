/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

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
                bool use_ts_bit { false };
                bool init { false };
                std::uint32_t last_restored { 0 };

                struct fpu_emulation_status
                {
                    bool mp : 1;
                    bool em : 1;
                    bool host_mp : 1;
                    bool host_em : 1;
                    enum
                    {
                        fpu_none = 0,
                        fpu_286 = 2,
                        fpu_387,
                        fpu_486
                    } fpu_type : 4;
                    unsigned : 8;
                };

                void set_fpu_emulation(bool em, bool mp = true)
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

                fpu_emulation_status get_fpu_emulation()
                {
                    fpu_emulation_status status;
                    asm volatile(
                        "int 0x31;"
                        : "=a" (status)
                        : "a" (0x0E00)
                        : "cc");
                    return status;
                }
                
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
                    if (!use_ts_bit) set_fpu_emulation(true);
                    else
                    {
                        cr0_t cr0 { };
                        cr0.task_switched = true;
                        cr0.set();
                    }
                }

                INTERRUPT void leave() noexcept
                {
                    if (!init) return;
                    if (contexts.back() != nullptr) alloc.deallocate(contexts.back(), 1);
                    contexts.pop_back();
                    bool switch_required = last_restored != (contexts.size() - 1);
                    if (!use_ts_bit) set_fpu_emulation(switch_required);
                    else
                    {
                        cr0_t cr0 { };
                        cr0.task_switched = switch_required;
                        cr0.set();
                    }
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
