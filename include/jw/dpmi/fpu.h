#pragma once
#include <array>
#include <memory>
#include <unordered_map>
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
                    if (!test_cr0_access()) return;
                    asm("mov %0, cr0;":"=r"(*this));
                }
                void set()
                {
                    if (!test_cr0_access()) return;
                    asm("mov cr0, %0;"::"r"(*this));
                }
            };

            class fpu_context_switcher_t
            {
                locked_pool_allocator<> alloc { config::interrupt_fpu_context_pool };
                std::unordered_map<std::uint32_t, fpu_context, std::hash<std::uint32_t>, std::equal_to<std::uint32_t>, locked_pool_allocator<>> contexts { alloc };
                fpu_context irq_context;
                bool lazy_switching { false };
                volatile std::uint32_t count { 0 };
                
                void save() { contexts[static_cast<std::uint32_t>(count) - 1].save(); irq_context.restore(); }
                void restore() { contexts[static_cast<std::uint32_t>(count)].restore(); }

            public:
                fpu_context_switcher_t();

                void enter()
                {   
                    ++count;
                    if (!lazy_switching) save();
                    cr0_t cr0 { };
                    cr0.task_switched = true;
                    cr0.set();
                }

                void leave()
                {
                    --count;
                    if (!lazy_switching) restore();
                    auto i = static_cast<std::uint32_t>(count) + 1;
                    contexts.erase(i);
                    cr0_t cr0 { };
                    cr0.task_switched = true;
                    cr0.set();
                }
            } extern fpu_context_switcher;
        }
    }
}