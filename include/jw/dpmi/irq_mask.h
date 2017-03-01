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

        // Disables the interrupt flag
        class interrupt_mask
        {
        public:
            interrupt_mask() { cli(); }
            ~interrupt_mask() { sti(); }

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
            irq_mask(unsigned int _irq) : irq(_irq) { cli(); }
            ~irq_mask() { sti(); }

        private:
            void cli()
            {
                if (map[irq].count++ > 0) return;   // FIXME: race condition here

                byte mask = 1 << (irq % 8);
                io::io_port<byte> port { irq < 8 ? pic0_data_port : pic1_data_port };

                byte current = port.read();
                map[irq].first = (current & mask) != 0;
                port.write(current | mask);
            }

            void sti()
            {
                if (map[irq].count == 0) return;
                if (--map[irq].count > 0) return;
                if (map[irq].first) return;  // good idea...?

                byte mask = 1 << (irq % 8);
                io::io_port<byte> port { irq < 8 ? pic0_data_port : pic1_data_port };

                port.write(port.read() & ~mask);
            }

            //const io::io_port<byte> pic0_data_port { 0x21 };
            //const io::io_port<byte> pic1_data_port { 0xA1 };

            static const io::port_num pic0_data_port { 0x21 };
            static const io::port_num pic1_data_port { 0xA1 };

            struct mask_counter
            {
                volatile int count { 0 };
                bool first;
            };
            static std::array<mask_counter, 16> map;

            unsigned int irq;
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
