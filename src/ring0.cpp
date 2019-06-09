/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */

#include <jw/dpmi/ring0.h>

namespace jw::dpmi
{
    ring0_privilege::ring0_privilege()
    {
        if (detail::ring0_cs == 0)
        {
            try
            {
                cs = descriptor::clone_segment(detail::ring3_cs);
                cs->segment.code_segment.privilege_level = 0;
                cs->set_selector_privilege(0);
                detail::ring0_cs = cs->get_selector();
                cs->write();
                ss = descriptor::clone_segment(detail::ring3_ss);
                ss->segment.code_segment.privilege_level = 0;
                ss->set_selector_privilege(0);
                detail::ring0_ss = ss->get_selector();
                ss->write();
                gate = descriptor::create_call_gate(detail::ring0_cs, reinterpret_cast<std::uintptr_t>(ring0_entry_point));
                gate->call_gate.privilege_level = 3;
                gate->call_gate.stack_params = 0;
                gate->write();
                entry.segment = gate->get_selector();
            }
            catch (...)
            {
                detail::ring0_cs = 0;
                cs.reset();
                ss.reset();
                gate.reset();
                std::throw_with_nested(no_ring0_access { });
            }
        }
        if (get_cs() != detail::ring0_cs) enter();
        else dont_leave = true;
    }

    ring0_privilege::~ring0_privilege() { if (not dont_leave) force_leave(); }

    void ring0_privilege::enter()
    {
        asm volatile
        (
            "mov %0, esp;"
            "call fword ptr %1;"
            : "=m" (esp)
            : "m" (entry)
        );
    }
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wstrict-aliasing"
    void ring0_privilege::leave()
    {
        asm
        (
            "lea eax, [esp+4];"
            "push %0;"          //  SS
            "push eax;"         //  ESP
            "push %1;"          //  CS
            "push [eax-4];"     //  EIP
            "retf;"
            :: "m" (reinterpret_cast<std::uint32_t&>(detail::ring3_ss))
            , "m" (reinterpret_cast<std::uint32_t&>(detail::ring3_cs))
            : "eax"
        );
    }
#   pragma GCC diagnostic pop

    void ring0_privilege::ring0_entry_point()
    {
        asm
        (
            "add esp, 0x10;"
            "mov ss, %w0;"
            "mov esp, %1;"
            "ret;"
            :: "m" (detail::ring0_ss)
            , "m" (esp)
        );
    }
}
