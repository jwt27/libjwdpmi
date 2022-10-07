/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <atomic>
#include <jw/io/ioport.h>
#include <jw/dpmi/dpmi.h>
#include <jw/dpmi/detail/selectors.h>
#include "jwdpmi_config.h"

namespace jw::dpmi::detail
{
    template<bool enable>
    struct interrupt_flag
    {
        interrupt_flag() noexcept : prev_state { get_and_set() } { }
        ~interrupt_flag() { restore(); }

        interrupt_flag(const interrupt_flag&) = delete;
        interrupt_flag(interrupt_flag&&) = delete;
        interrupt_flag& operator=(const interrupt_flag&) = delete;
        interrupt_flag& operator=(interrupt_flag&&) = delete;

    private:
        constexpr static bool use_dpmi = config::support_virtual_interrupt_flag;

        static std::uint32_t get_and_set()
        {
            if constexpr (use_dpmi)
            {
                std::uint16_t ax = 0x0900 | enable;
                asm volatile ("int 0x31" : "+a" (ax) :: "cc");
                return ax;
            }
            else
            {
                std::uint32_t flags;
                if constexpr (enable)
                    asm volatile ("pushfd; sti; pop %0" : "=rm" (flags));
                else
                    asm volatile ("pushfd; cli; pop %0" : "=rm" (flags));
                return flags;
            }
        }

        void restore()
        {
            if constexpr (use_dpmi and enable)
            {
                if (not (prev_state & 1)) [[likely]] { asm ("cli"); }
            }
            else if constexpr (use_dpmi and not enable)
            {
                if (prev_state & 1) [[likely]] { asm ("sti"); }
            }
            else
            {
                asm volatile ("push %0; popfd" :: "rm" (prev_state) : "cc");
            }
        }

        const std::uint32_t prev_state;
    };
}

namespace jw::dpmi
{
    using int_vector = std::uint8_t;
    using irq_level = std::uint8_t;

    inline bool interrupts_enabled() noexcept
    {
        if constexpr (config::support_virtual_interrupt_flag)
        {
            std::uint16_t ax = 0x0902;
            asm ("int 0x31" : "+a" (ax) :: "cc");
            return ax & 1;
        }
        else return cpu_flags::current().interrupts_enabled;
    }

    // Disables the interrupt flag
    using interrupt_mask = detail::interrupt_flag<false>;

    // Enables the interrupt flag
    using interrupt_unmask = detail::interrupt_flag<true>;

    struct async_signal_mask
    {
        async_signal_mask() noexcept
        {
            asm("mov %0, ss" : "=r" (ss));
            asm("mov %0, ds" : "=r" (ds));
            asm volatile
            (R"(
                mov ss, %k0
                mov ds, %k0
                mov es, %k0
             )" :
                : "r"  (detail::safe_ds),
                  "rm" (ss),
                  "rm" (ds)
            );
        }

        ~async_signal_mask()
        {
            asm volatile
            (R"(
                mov ss, %k0
                mov ds, %k1
                mov es, %k1
             )" :
                : "r" (ss),
                  "r" (ds)
            );
        }

    private:
        async_signal_mask(async_signal_mask&&) = delete;
        async_signal_mask(const async_signal_mask&) = delete;
        async_signal_mask& operator=(async_signal_mask&&) = delete;
        async_signal_mask& operator=(const async_signal_mask&) = delete;

        std::uint32_t ss;
        std::uint32_t ds;
    };

    // Masks one specific IRQ.
    // note: involves IO ports, so this may be slower than disabling interrupts altogether
    class irq_mask
    {
    public:
        irq_mask(irq_level _irq) noexcept : irq(_irq) { cli(); }
        ~irq_mask() { sti(); }

        irq_mask(const irq_mask&) = delete;
        irq_mask(irq_mask&&) = delete;
        irq_mask& operator=(const irq_mask&) = delete;
        irq_mask& operator=(irq_mask&&) = delete;

        static void unmask(irq_level irq) noexcept // TODO: raii unmask
        {
            auto& m = map[irq];
            auto c = m.count.load() & ~(1UL << 31);
            if (c > 0)
            {
                m.count.store(c);
            }
            else
            {
                auto [mask, port] = mp(irq);
                port.write(port.read() & ~mask);
            }
        }

        static bool enabled(irq_level irq) noexcept
        {
            if ((map[irq].count.load() & ~(1UL << 31)) > 0) return false;

            auto [mask, port] = mp(irq);
            return port.read() & mask;
        }

    private:
        void cli() noexcept
        {
            auto [mask, port] = mp(irq);
            byte current = port.read();
            port.write(current | mask);

            auto& m = map[irq];
            auto c = m.count.load();
            if ((c & ~(1UL << 31)) == 0) c = ((current & mask) != 0) << 31;
            m.count.store(c + 1);
        }

        void sti() noexcept
        {
            auto& m = map[irq];
            auto c = m.count.load() - 1;
            m.count.store(c);
            if (c == 0)
            {
                auto [mask, port] = mp(irq);
                port.write(port.read() & ~mask);
            }
        }

        static constexpr std::tuple<byte, io::io_port<byte>> mp(irq_level irq)
        {
            byte mask = 1 << (irq % 8);
            auto& port = irq < 8 ? pic0_data : pic1_data;
            return { mask, port };
        }

        static inline constexpr io::io_port<byte> pic0_data { 0x21 };
        static inline constexpr io::io_port<byte> pic1_data { 0xA1 };

        struct mask_counter
        {
            std::atomic<std::uint32_t> count { 0 };   // MSB is initial state (unset if irq enabled)
            constexpr mask_counter() noexcept { }
        };
        static inline std::array<mask_counter, 16> map { };

        irq_level irq;
    };
}
