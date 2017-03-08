#pragma once
#include <array>
#include <memory>
#include <atomic>
#include <deque>
#include <jw/dpmi/irq_check.h>
#include <jw/dpmi/lock.h>
#include <jw/dpmi/alloc.h>
#include <jw/typedef.h>
#include <../jwdpmi_config.h>

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
                    asm volatile("mov %0, cr0;":"=r"(*this):"rm"(cr0_access));  // needs the result of test_cr0_access() here
                }                                                               // or else gcc may place the asm before the function call...
                void set()
                {
                    bool cr0_access = test_cr0_access();
                    if (!cr0_access) return;
                    asm volatile("mov cr0, %0;"::"r"(*this),"rm"(cr0_access));
                }
            };

            class fpu_context_switcher_t : class_lock<fpu_context_switcher_t>
            {
                locked_pool_allocator<> alloc { config::interrupt_fpu_context_pool };
                std::deque<std::shared_ptr<fpu_context>, locked_pool_allocator<>> contexts { alloc };
                
                fpu_context default_irq_context;
                bool lazy_switching { false };
                bool init { false };
                std::uint32_t last_restored { 0 };
                
                [[gnu::optimize("no-tree-vectorize")]]  // TODO: find some way to disable x87/sse instructions altogether.
                void switch_context()
                {
                    if (contexts.back() == nullptr)
                    {
                        if (last_restored < contexts.size() - 1)
                        {
                            if (contexts[last_restored] == nullptr) contexts[last_restored] = std::allocate_shared<fpu_context>(alloc); // TODO: check if this does zero-initialization (it shouldn't)
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

                [[gnu::optimize("no-tree-vectorize")]]
                void enter() noexcept
                {
                    if (!init) return;
                    contexts.push_back(nullptr);
                    if (!lazy_switching) switch_context();
                    cr0_t cr0 { };
                    cr0.task_switched = true;
                    cr0.set();
                }

                [[gnu::optimize("no-tree-vectorize")]]
                void leave() noexcept
                {
                    if (!init) return;
                    contexts.pop_back();
                    if (!lazy_switching) switch_context();
                    cr0_t cr0 { };
                    cr0.task_switched = true;
                    cr0.set();
                }
            } extern fpu_context_switcher;
        }
    }
}