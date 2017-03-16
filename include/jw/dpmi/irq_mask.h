/******************************* libjwdpmi **********************************
Copyright (C) 2016-2017  J.W. Jagersma

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// Interrupt and IRQ masking classes. 

#pragma once
#include <iostream>
#include <unordered_map>
#include <atomic>

#include <jw/io/ioport.h>
#include <jw/dpmi/dpmi.h>
//#include "debug.h"

namespace jw
{
    namespace dpmi
    {
        //TODO: lock()/unlock() for use with std::lock_guard ?
        using irq_level = std::uint8_t;

        // Disables the interrupt flag
        class interrupt_mask
        {
        public:
            interrupt_mask() noexcept { cli(); }
            ~interrupt_mask() { sti(); }
            
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

        private:
            // Disables the interrupt flag
            static void cli() noexcept
            {
                auto state = get_and_set_interrupt_state(false);
                if (count++ == 0) initial_state = state;
            }

            // Restores the interrupt flag to previous state
            static void sti() noexcept
            {
                if (count == 0) return;
                if (--count == 0 && initial_state) asm("sti");
            }

            static volatile int count;
            static bool initial_state;

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

                asm volatile (
                    "int 0x31;"
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
                auto port = irq < 8 ? pic0_data : pic1_data;
                port.write(port.read() & ~mask);
            }

        private:
            void cli() noexcept
            {
                if (map[irq].count++ > 0) return;   // FIXME: race condition here

                byte mask = 1 << (irq % 8);
                auto port = irq < 8 ? pic0_data : pic1_data;

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
                auto port = irq < 8 ? pic0_data : pic1_data;

                port.write(port.read() & ~mask);
            }

            static constexpr io::io_port<byte> pic0_data { 0x21 };
            static constexpr io::io_port<byte> pic1_data { 0xA1 };

            struct mask_counter
            {
                volatile int count { 0 };
                bool first; // true if initially masked
            };
            static std::array<mask_counter, 16> map;

            irq_level irq;
        };

        /*
        class nmi_mask  // lol who the hell would ever want to use this
        {
        public:
             nmi_mask() { cli(); }
            ~nmi_mask() { sti(); }

        private:
            void cli()
            {
                if (count++ > 0) return;

                outportb(rtc_index_port, 0x80);
                inportb(rtc_data_port);
            }

            void sti()
            {
                if (count == 0) return;
                if (--count > 0) return;

                outportb(rtc_index_port, 0x00);
                inportb(rtc_data_port);
            }

            const unsigned short rtc_index_port = 0x70;
            const unsigned short rtc_data_port = 0x71;

            static int count;
        };*/
    }
}
