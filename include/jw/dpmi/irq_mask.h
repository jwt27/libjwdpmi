/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <iostream>
#include <unordered_map>
#include <atomic>
#include <jw/io/ioport.h>
#include <jw/dpmi/dpmi.h>
#include "jwdpmi_config.h"

namespace jw
{
    namespace dpmi
    {
        using int_vector = std::uint8_t;
        using irq_level = std::uint8_t;

        // Disables the interrupt flag
        struct interrupt_mask
        {
            interrupt_mask() noexcept : state { cli() } { }
            ~interrupt_mask() { sti(); }

            interrupt_mask(const interrupt_mask&) = delete;
            interrupt_mask(interrupt_mask&&) = delete;
            interrupt_mask& operator=(const interrupt_mask&) = delete;
            interrupt_mask& operator=(interrupt_mask&&) = delete;

            // Get the current interrupt flag state
            // true == interrupts enabled
            static bool interrupts_enabled() noexcept
            {
                if constexpr (use_dpmi)
                {
                    std::uint32_t state;
                    asm ("int 0x31;"
                        : "=a" (state)
                        : "a" (0x0902)
                        : "cc");
                    return state & 1;
                }
                else return cpu_flags::current().interrupts_enabled;
            }

        private:
            constexpr static bool use_dpmi = config::support_virtual_interrupt_flag;

            static std::uint32_t cli()
            {
                if constexpr (use_dpmi)
                {
                    std::uint32_t eax = 0x0900;
                    asm volatile ("int 0x31" : "+a" (eax) :: "cc");
                    return eax;
                }
                else
                {
                    std::uint32_t flags;
                    asm volatile ("pushfd; cli; pop %0" : "=rm" (flags));
                    return flags;
                }
            }

            void sti()
            {
                if constexpr (use_dpmi)
                {
                    asm volatile ("int 0x31" :: "a" (state) : "cc");
                }
                else
                {
                    asm volatile ("push %0; popfd" :: "rm" (state) : "cc");
                }
            }

            const std::uint32_t state;
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

            static constexpr std::tuple<byte, io::io_port<byte>&> mp(irq_level irq)
            {
                byte mask = 1 << (irq % 8);
                auto& port = irq < 8 ? pic0_data : pic1_data;
                return { mask, port };
            }

            static inline constinit io::io_port<byte> pic0_data { 0x21 };
            static inline constinit io::io_port<byte> pic1_data { 0xA1 };

            struct mask_counter
            {
                std::atomic<std::uint32_t> count { 0 };   // MSB is initial state (unset if irq enabled)
                constexpr mask_counter() noexcept { }
            };
            static inline std::array<mask_counter, 16> map { };

            irq_level irq;
        };
    }
}
