/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */

#include <jw/dpmi/ring0.h>

namespace jw::dpmi
{
    enum { unknown, yes, no } static ring0_accessible { unknown };

    void ring0_privilege::setup(bool throw_on_fail)
    {
        if (ring0_accessible != unknown) return;
        try
        {
            detail::ring3_cs = get_cs();
            detail::ring3_ss = get_ss();
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
            ring0_accessible = yes;
        }
        catch (...)
        {
            detail::ring0_cs = 0;
            cs.reset();
            ss.reset();
            gate.reset();
            ring0_accessible = no;
            if (throw_on_fail) std::throw_with_nested(no_ring0_access { });
        }
    }

    ring0_privilege::ring0_privilege()
    {
        if (ring0_accessible == unknown) [[unlikely]] setup(true);
        if (ring0_accessible != yes) throw no_ring0_access { };
        if (get_cs() != detail::ring0_cs) enter();
        else dont_leave = true;
    }

    ring0_privilege::~ring0_privilege() { if (not dont_leave) force_leave(); }

    bool ring0_privilege::wont_throw()
    {
        if (ring0_accessible == unknown) [[unlikely]] setup(false);
        return ring0_accessible == yes;
    }

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

    void ring0_privilege::leave()
    {
        asm
        (
            "lea eax, [esp+4];"
            "push %k0;"         //  SS
            "push eax;"         //  ESP
            "push %k1;"         //  CS
            "push [eax-4];"     //  EIP
            "retf;"
            :: "m" (detail::ring3_ss)
            , "m" (detail::ring3_cs)
            : "eax"
        );
    }

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
