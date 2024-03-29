/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */

#include <jw/dpmi/ring0.h>
#include <jw/dpmi/detail/selectors.h>

namespace jw::dpmi
{
    enum { unknown, yes, no } static constinit ring0_accessible { unknown };
    static constinit std::size_t ring0_selector_limit { 0 };

    void ring0_privilege::setup(bool throw_on_fail)
    {
        if (ring0_accessible != unknown) return;
        try
        {
            cs = descriptor::clone_segment(detail::main_cs);
            auto data = cs->read();
            data.segment.code_segment.privilege_level = 0;
            cs->set_selector_privilege(0);
            detail::ring0_cs = cs->get_selector();
            cs->write(data);

            ss = descriptor::clone_segment(detail::safe_ds);
            data = ss->read();
            data.segment.code_segment.privilege_level = 0;
            ss->set_selector_privilege(0);
            detail::ring0_ss = ss->get_selector();
            ss->write(data);

            gate = descriptor::create_call_gate(detail::ring0_cs, reinterpret_cast<std::uintptr_t>(ring0_entry_point));
            data = gate->read();
            data.call_gate.privilege_level = 3;
            data.call_gate.stack_params = 0;
            gate->write(data);
            if (gate->read().call_gate.not_system_segment) throw std::runtime_error { "Failed to create call gate" };

            entry.segment = gate->get_selector();
            ring0_selector_limit = __djgpp_selector_limit;
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
        selector_bits current_cs { get_cs() };
        if (current_cs.privilege_level != 0)
        {
            if (ring0_accessible == unknown) [[unlikely]] setup(true);
            if (ring0_accessible != yes) throw no_ring0_access { };
            if (ring0_selector_limit < static_cast<std::size_t>(__djgpp_selector_limit))
            {
                cs->set_limit(__djgpp_selector_limit);
                ss->set_limit(__djgpp_selector_limit);
                ring0_selector_limit = __djgpp_selector_limit;
            }
            ring3_ds = get_ds();
            enter();
        }
        else dont_leave = true;
    }

    ring0_privilege::~ring0_privilege()
    {
        if (not dont_leave) leave();
    }

    bool ring0_privilege::wont_throw() noexcept
    {
        selector_bits cs { get_cs() };
        if (cs.privilege_level == 0) return true;
        if (ring0_accessible == unknown) [[unlikely]] setup(false);
        return ring0_accessible == yes;
    }

    void ring0_privilege::force_leave() noexcept
    {
        if (get_cs() == detail::ring0_cs) leave();
    }

    void ring0_privilege::enter()
    {
        asm volatile
        (R"(
            mov %0, esp
            call fword ptr %1
         )" : "=m" (esp)
            : "m" (entry)
        );
    }

    void ring0_privilege::leave()
    {
        asm
        (R"(
            mov edx, %k0
            lea eax, [esp+4]
            push edx            #  SS
            push eax            #  ESP
            push %k1            #  CS
            push [eax-4]        #  EIP
            mov ds, edx
            mov es, edx
            retf
         )" :
            : "m" (ring3_ds)
            , "m" (detail::main_cs)
            : "eax", "edx"
        );
    }

    void ring0_privilege::ring0_entry_point()
    {
        asm
        (R"(
            mov eax, %k0
            mov edx, %k1
            mov ss, eax
            mov esp, %2
            mov ds, edx
            mov es, edx
            ret
         )" :
            : "m" (detail::ring0_ss)
            , "m" (detail::safe_ds)
            , "m" (esp)
            : "eax", "edx"
        );
    }
}
