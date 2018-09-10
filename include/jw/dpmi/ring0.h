/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <optional>
#include <jw/dpmi/memory.h>
#include <jw/dpmi/cpu_exception.h>

namespace jw::dpmi
{
    namespace detail
    {
        inline selector ring0_cs { 0 };
        inline selector ring3_cs { get_cs() };
    }

    struct no_ring0_access : std::runtime_error
    {
        no_ring0_access() : runtime_error("Switch to ring 0 failed.") { }
    };

    struct ring0_privilege
    {
        ring0_privilege()
        {
            if (detail::ring0_cs == 0)
            {
                try
                {
                    cs = descriptor::clone_segment(get_cs());
                    cs->segment.code_segment.privilege_level = 0;
                    cs->set_selector_privilege(0);
                    cs->write();
                    gate = descriptor::create_call_gate(cs->get_selector(), reinterpret_cast<std::uintptr_t>(ring0_entry_point));
                    gate->call_gate.privilege_level = 3;
                    gate->call_gate.stack_params = 0;
                    cs->write();
                    detail::ring0_cs = gate->get_selector();
                    entry.segment = detail::ring0_cs;
                }
                catch (...)
                {
                    detail::ring0_cs = 0;
                    cs.reset();
                    gate.reset();
                    std::throw_with_nested(no_ring0_access { });
                }
            }

            if (get_cs() != detail::ring0_cs) enter();
            else dont_leave = true;
        }
        ~ring0_privilege() { if (not dont_leave) leave(); }


    private:
        inline static std::optional<descriptor> cs;
        inline static std::optional<descriptor> gate;
        inline static far_ptr32 entry;
        inline static std::uintptr_t esp;
        bool dont_leave { false };
        
        [[gnu::naked, gnu::noinline]] void enter()
        {
            asm("mov %1, esp;"
                "call fword ptr %0;"
                :: "m" (entry)
                , "m" (esp));
        }

#       pragma GCC diagnostic push
#       pragma GCC diagnostic ignored "-Wstrict-aliasing"
        [[gnu::naked, gnu::noinline]] void leave()
        {
            asm("mov eax, esp;"
                "push ds;"      //  SS
                "push eax;"     //  ESP
                "push %0;"      //  CS
                "push [eax];"   //  EIP
                "retf;"
                :: "m" (reinterpret_cast<std::uint32_t&>(detail::ring3_cs))
                : "eax");
        }
#       pragma GCC diagnostic pop

        [[gnu::naked]] static void ring0_entry_point()
        {
            asm("add esp, 0x10;"
                "push ds;"
                "pop ss;"
                "mov esp, %0;"
                "ret;"
                ::"m" (esp));
        }
    };
}
