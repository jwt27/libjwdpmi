/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
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

namespace jw
{
    namespace dpmi
    {
        using irq_level = std::uint8_t;

        // Disables the interrupt flag
        class interrupt_mask
        {
        public:
            interrupt_mask() noexcept { cli(); }
            ~interrupt_mask() { if ((--count | not initially_enabled) == 0) [[likely]] { asm ("sti"); } }

            interrupt_mask(const interrupt_mask&) = delete;
            interrupt_mask(interrupt_mask&&) = delete;
            interrupt_mask& operator=(const interrupt_mask&) = delete;
            interrupt_mask& operator=(interrupt_mask&&) = delete;

            // Get the current interrupt flag state
            // true == interrupts enabled
            static bool enabled() noexcept
            {
                return get_interrupt_state();
            }

            // Enables the interrupt flag
            static void sti() noexcept
            {
                if (count > 0) return;
                asm ("sti");
            }

        private:
            // Disables the interrupt flag
            static void cli() noexcept
            {
                auto state = get_and_set_interrupt_state(false);
                if (count++ == 0) initially_enabled = state;
            }

            static inline std::uint32_t count { 0 };
            static inline bool initially_enabled;

            //DPMI 0.9 AX=090x
            static bool get_and_set_interrupt_state(bool state) noexcept
            {
                asm volatile (
                    "int 0x31;"
                    : "=a" (state)
                    : "a" ((state & 1) | 0x0900)
                    : "cc");

                return state & 1;
            }

            //DPMI 0.9 AX=0902
            static bool get_interrupt_state() noexcept
            {
                std::uint32_t state;

                asm("int 0x31;"
                    : "=a" (state)
                    : "a" (0x0902)
                    : "cc");

                return state & 1;
            }
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
                asm("irq_mask_cli%=:":::);
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
