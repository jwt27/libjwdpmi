/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
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

    enum class interrupt_type : std::uint8_t
    {
        none,
        exception,
        irq,
        realmode_irq
    };

    enum class ack : std::uint8_t
    {
        no,
        eoi_sent,
        yes
    };

    struct interrupt_id_data
    {
        fpu_registers* const fpu;
        const std::uint64_t id { id_count++ };
        const std::uint32_t num;
        const interrupt_type type;
        interrupt_id_data* const next;
        ack acknowledged { type == interrupt_type::irq ? ack::no : ack::yes };
        jw::detail::jw_cxa_eh_globals eh_globals;

    protected:
        friend struct jw::init;
        friend struct interrupt_id;
        static inline constinit std::uint64_t id_count { 0 };

        interrupt_id_data() noexcept : fpu { nullptr }, num { 0 }, type { interrupt_type::none }, next { nullptr } { }

        interrupt_id_data(fpu_registers* f, std::uint32_t n, interrupt_type t, interrupt_id_data* current) noexcept
            : fpu { f }, num { n }, type { t }, next { current } { }
    };

    struct interrupt_id : interrupt_id_data
    {
        interrupt_id(empty*, std::uint32_t n, interrupt_type t) noexcept : interrupt_id { static_cast<fpu_context*>(nullptr), n, t } { }

        interrupt_id(fpu_context* f, std::uint32_t n, interrupt_type t) noexcept : interrupt_id_data { &f->registers, n, t, current }
        {
            ++interrupt_count;
            next->eh_globals = jw::detail::get_eh_globals();
            jw::detail::set_eh_globals({ });
            current = this;
        }

        ~interrupt_id()
        {
            current = next;
            jw::detail::set_eh_globals(next->eh_globals);
            --interrupt_count;
        }

        static interrupt_id_data* get() noexcept { return current; }
        static const std::uint64_t& get_id() noexcept { return current->id; }

        static bool is_live(std::uint64_t id) noexcept
        {
            for (auto* p = current; p != nullptr; p = p->next)
                if (p->id == id) return true;
            return false;
        }

    private:
        friend struct jw::init;
        static inline constinit std::uint64_t id_count { 0 };
        static inline interrupt_id_data* current;

        static void setup() noexcept
        {
            static interrupt_id_data id { };
            current = &id;
        }
    };
}
