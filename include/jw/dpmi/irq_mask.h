/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
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
            ~interrupt_mask() { if (__builtin_expect(--count == 0 && initial_state, true)) sti(); }
            
            interrupt_mask(const interrupt_mask&) = delete;
            interrupt_mask(interrupt_mask&&) = delete;
            interrupt_mask& operator=(const interrupt_mask&) = delete;
            interrupt_mask& operator=(interrupt_mask&&) = delete;

            // Get the current interrupt flag state
            // true == interrupts enabled
            static bool get() noexcept
            {
                return get_interrupt_state();
            }

            // Enables the interrupt flag
            static void sti() noexcept
            {
                if (count > 0) return;
                asm("sti");
            }

        private:
            // Disables the interrupt flag
            static void cli() noexcept
            {
                auto state = get_and_set_interrupt_state(false);
                if (count++ == 0) initial_state = state;
            }

            static inline volatile int count { 0 };
            static inline bool initial_state;

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

            static void unmask(irq_level irq) // TODO: raii unmask
            {
                if (map[irq].count > 0) { map[irq].first = false; return; }

                byte mask = 1 << (irq % 8);
                auto& port = irq < 8 ? pic0_data : pic1_data;
                port.write(port.read() & ~mask);
            }

        private:
            void cli() noexcept
            {
                if (map[irq].count++ > 0) return;   // FIXME: race condition here

                byte mask = 1 << (irq % 8);
                auto& port = irq < 8 ? pic0_data : pic1_data;

                byte current = port.read();
                map[irq].first = (current & mask) != 0;
                port.write(current | mask);
            }

            void sti() noexcept
            {
                if (map[irq].count == 0) return;
                if (--map[irq].count > 0) return;
                if (map[irq].first) return;

                byte mask = 1 << (irq % 8);
                auto& port = irq < 8 ? pic0_data : pic1_data;

                port.write(port.read() & ~mask);
            }

            static inline constexpr io::io_port<byte> pic0_data { 0x21 };
            static inline constexpr io::io_port<byte> pic1_data { 0xA1 };

            struct mask_counter
            {
                volatile int count { 0 };
                bool first { }; // true if initially masked
                constexpr mask_counter() noexcept { }
            };
            static inline std::array<mask_counter, 16> map { };

            irq_level irq;
        };
    }
}
