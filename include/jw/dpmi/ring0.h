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
        inline selector ring0_ss { 0 };
        inline selector ring3_ss { get_ss() };
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
        ~ring0_privilege() { if (not dont_leave) force_leave(); }

        static void force_leave()
        {
            if (get_cs() == detail::ring0_cs) leave();
        }

    private:
        inline static std::optional<descriptor> cs;
        inline static std::optional<descriptor> ss;
        inline static std::optional<descriptor> gate;
        inline static far_ptr32 entry;
        inline static std::uintptr_t esp;
        bool dont_leave { false };
        
        [[gnu::naked, gnu::noinline]] static void enter()
        {
            asm("mov %1, esp;"
                "call fword ptr %0;"
                :: "m" (entry)
                , "m" (esp));
        }

#       pragma GCC diagnostic push
#       pragma GCC diagnostic ignored "-Wstrict-aliasing"
        [[gnu::naked, gnu::noinline]] static void leave()
        {
            asm("mov eax, esp;"
                "push %0;"      //  SS
                "push eax;"     //  ESP
                "push %1;"      //  CS
                "push [eax];"   //  EIP
                "retf;"
                :: "m" (reinterpret_cast<std::uint32_t&>(detail::ring3_ss))
                , "m" (reinterpret_cast<std::uint32_t&>(detail::ring3_cs))
                : "eax");
        }

        [[gnu::naked]] static void ring0_entry_point()
        {
            asm("add esp, 0x10;"
                "mov ss, %0;"
                "mov esp, %1;"
                "ret;"
                :: "m" (reinterpret_cast<std::uint32_t&>(detail::ring0_ss))
                , "m" (esp));
        }
#       pragma GCC diagnostic pop
    };
}
