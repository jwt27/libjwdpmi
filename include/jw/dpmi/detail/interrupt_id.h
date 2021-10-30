/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstdint>
#include <jw/dpmi/fpu.h>
#include <jw/main.h>
#include <jw/detail/eh_globals.h>

namespace jw::dpmi::detail
{
    inline constinit std::uint32_t interrupt_count { 0 };

    enum class interrupt_type
    {
        none,
        exception,
        irq,
        realmode,
        realmode_irq
    };

    enum class ack
    {
        no,
        eoi_sent,
        yes
    };

    struct interrupt_id_data
    {
        const std::uint64_t id { id_count++ };
        const std::uint32_t num;
        const interrupt_type type;
        interrupt_id_data* const next;
        interrupt_id_data* const next_fpu;
        ack acknowledged { type == interrupt_type::irq ? ack::no : ack::yes };
        jw::detail::jw_cxa_eh_globals eh_globals;
        bool fpu_context_switched { false };
        bool has_fpu_context { false };
        fpu_context fpu;

    protected:
        friend struct jw::init;
        friend struct interrupt_id;
        static inline constinit std::uint64_t id_count { 0 };

        interrupt_id_data() noexcept : num { 0 }, type { interrupt_type::none }, next { nullptr }, next_fpu { this } { }

        interrupt_id_data(std::uint32_t n, interrupt_type t, interrupt_id_data* current, interrupt_id_data* current_fpu) noexcept
            : num { n }, type { t }, next { current }, next_fpu { current_fpu } { }
    };

    struct interrupt_id : interrupt_id_data
    {
        interrupt_id(std::uint32_t n, interrupt_type t) noexcept : interrupt_id_data { n, t, current, current_fpu }
        {
            switch (type)
            {
            case interrupt_type::exception:
            case interrupt_type::irq:
            case interrupt_type::realmode_irq:
                ++interrupt_count;
                break;

            case interrupt_type::realmode: break;
            default: __builtin_unreachable();
            }
            next->eh_globals = jw::detail::get_eh_globals();
            jw::detail::set_eh_globals({ });
            fpu_enter();
            current = this;
        }

        ~interrupt_id()
        {
            current = next;
            fpu_leave();
            jw::detail::set_eh_globals(next->eh_globals);
            switch (type)
            {
            case interrupt_type::exception:
            case interrupt_type::irq:
            case interrupt_type::realmode_irq:
                --interrupt_count;
                break;

            case interrupt_type::realmode: break;
            default: __builtin_unreachable();
            }
        }

        static interrupt_id_data* get() noexcept { return current; }
        static const std::uint64_t& get_id() noexcept { return current->id; }

        static bool is_live(std::uint64_t id) noexcept
        {
            for (auto* p = current; p != nullptr; p = p->next)
                if (p->id == id) return true;
            return false;
        }

        static fpu_context* last_fpu_context()
        {
            asm volatile ("fnop;fwait;":::"memory");   // force a context switch
            return &current->next_fpu->fpu;
        }

    private:
        friend struct jw::init;
        static inline constinit std::uint64_t id_count { 0 };
        static inline interrupt_id_data* current;       // Current context
        static inline interrupt_id_data* current_fpu;   // Last context where the FPU was used

        [[gnu::hot]] void fpu_enter();
        [[gnu::hot]] void fpu_leave();

        static void setup_fpu();

        static void setup() noexcept
        {
            static interrupt_id_data id { };
            current = &id;
            current_fpu = &id;
            setup_fpu();
        }
    };
}
