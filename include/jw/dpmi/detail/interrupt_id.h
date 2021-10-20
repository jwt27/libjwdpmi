/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstdint>
#include <cxxabi.h>
#include <jw/main.h>

namespace jw::dpmi::detail
{
    struct jw_cxa_eh_globals
    {
        void* caughtExceptions { nullptr };
        unsigned int uncaughtExceptions { 0 };
    };

    enum class interrupt_type
    {
        none,
        exception,
        irq,
        realmode,
        realmode_irq
    };

    struct interrupt_id;

    struct interrupt_id_data
    {
        const std::uint64_t id { id_count++ };
        const std::uint32_t num;
        const interrupt_type type;
        bool acknowledged { type != interrupt_type::irq };
        jw_cxa_eh_globals eh_globals { };
        interrupt_id_data* const next;

        static interrupt_id_data* main() noexcept
        {
            static interrupt_id_data id { };
            return &id;
        }

    protected:
        friend struct jw::init;
        friend struct interrupt_id;
        static inline constinit std::uint64_t id_count { 0 };

        interrupt_id_data() noexcept : num { 0 }, type { interrupt_type::none }, next { nullptr } { }

        interrupt_id_data(std::uint32_t n, interrupt_type t, interrupt_id_data* current) noexcept
            : num { n }, type { t }, next { current } { }
    };

    struct interrupt_id : interrupt_id_data
    {
        interrupt_id(std::uint32_t n, interrupt_type t) noexcept : interrupt_id_data { n, t, current }
        {
            next->eh_globals = *reinterpret_cast<jw_cxa_eh_globals*>(abi::__cxa_get_globals());
            *reinterpret_cast<jw_cxa_eh_globals*>(abi::__cxa_get_globals()) = eh_globals;
            current = this;
        }

        ~interrupt_id()
        {
            current = next;
            *reinterpret_cast<jw_cxa_eh_globals*>(abi::__cxa_get_globals()) = current->eh_globals;
        }

        static interrupt_id_data* main() noexcept
        {
            static interrupt_id_data id { };
            return &id;
        }

        static interrupt_id_data* get() noexcept { return current; }
        static const std::uint64_t& get_id() noexcept { return current->id; }

        static bool is_live(std::uint64_t id) noexcept
        {
            for (auto* p = current; p != nullptr; p = p->next)
                if (p->id == id) return true;
            return false;
        }

        static void acknowledge() noexcept
        {
            current->acknowledged = true;
        }

    private:
        friend struct jw::init;
        static inline constinit std::uint64_t id_count { 0 };
        static inline interrupt_id_data* current;

        static void setup() noexcept
        {
            current = main();
        }
    };
}
