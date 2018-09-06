/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <optional>
#include <jw/dpmi/memory.h>
#include <jw/dpmi/cpu_exception.h>

namespace jw::dpmi
{
    inline selector ring0_cs { 0 };
    inline selector ring3_cs { get_cs() };

    struct ring0_privilege
    {
        ring0_privilege()
        {
            try
            {
                if (ring0_cs == 0)
                {
                    cs = descriptor::create_alias(get_cs());
                    cs->segment.privilege_level = 0;
                    cs->set_selector_privilege(0);
                    cs->write();
                    gate = descriptor::create_call_gate(cs->get_selector(), reinterpret_cast<std::uintptr_t>(ring0_entry_point));
                    gate->call_gate.privilege_level = 3;
                    gate->call_gate.stack_params = 0;
                    cs->write();
                    ring0_cs = gate->get_selector();
                    entry.segment = ring0_cs;
                }
            }
            catch (const dpmi_error& e)
            {
                dont_leave = true;
                return;
            }
            catch (const cpu_exception& e)
            {
                if (e.code().value() != exception_num::general_protection_fault) throw;
                dont_leave = true;
                return;
            }

            if (get_cs() != ring0_cs) enter();
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

        [[gnu::naked, gnu::noinline]] void leave()
        {
            asm("mov eax, esp;"
                "push ds;"
                "push eax;"
                "push %0;"
                "push [eax];"
                "retf;"
                :: "m" (ring3_cs));
        }

        [[gnu::naked]] static void ring0_entry_point()
        {
            asm("add esp, 0x10;"
                "push ds;"
                "pop ss;"
                "mov esp, %0;"
                "ret;"
                ::"m"(esp));
        }
    };
}
