/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <jw/chrono/chrono.h>
#include <jw/io/ioport.h>
#include <jw/dpmi/irq_mask.h>

namespace jw
{
    namespace chrono
    {
        std::size_t tsc_max_sample_size { 1000 };
        std::size_t tsc_sample_size { 0 };
        std::uint64_t tsc_total { 0 };
        bool tsc_resync { true };

        constexpr io::out_port<byte> pit_cmd { 0x43 };
        constexpr io::io_port<byte> pit0_data { 0x40 };

        void setup::update_tsc()
        {
            static std::uint64_t last_tsc;
            auto tsc = rdtsc();
            std::uint32_t diff = tsc - last_tsc;
            last_tsc = tsc;
            if (__builtin_expect(tsc_resync, false)) { tsc_resync = false; return; }
            tsc_total += diff;
            ++tsc_sample_size;
            while (tsc_sample_size > tsc_max_sample_size)
            {
                tsc_total -= static_cast<std::uint32_t>(tsc_total / tsc_sample_size);
                --tsc_sample_size;
            }
            tsc_ticks_per_irq = tsc_total / tsc_sample_size;
        }

        dpmi::irq_handler setup::rtc_irq { []() INTERRUPT
        {
            static byte last_sec { 0 };
            dpmi::interrupt_mask no_irq { };

            rtc_index.write(0x80);
            auto sec = rtc_data.read();
            if (sec != last_sec)
            {
                rtc_ticks = 0;
                last_sec = sec;
            }
            else ++rtc_ticks;

            rtc_index.write(0x0C);
            rtc_data.read();

            if (current_tsc_ref() == tsc_reference::rtc) update_tsc();

            dpmi::end_of_interrupt();
        }, dpmi::always_call | dpmi::no_interrupts };

        dpmi::irq_handler setup::pit_irq { []() INTERRUPT
        {
            ++pit_ticks;

            if (current_tsc_ref() == tsc_reference::pit) update_tsc();

            dpmi::end_of_interrupt();
        }, dpmi::always_call | dpmi::no_auto_eoi };

        void setup::setup_pit(bool enable, std::uint32_t freq_divider)
        {
            dpmi::interrupt_mask no_irq { };
            reset_pit();
            if (!enable) return;

            if (freq_divider < 1 || freq_divider > 0x10000) 
                throw std::out_of_range("PIT frequency divisor must be a value between 1 and 0x10000, inclusive.");
            ns_per_pit_tick = 1e9 / (max_pit_frequency / freq_divider);
            pit_irq.set_irq(0);
            pit_irq.enable();

            split_uint16_t div { freq_divider };
            pit_cmd.write(0x34);
            pit0_data.write(div.lo);
            pit0_data.write(div.hi);
        }

        void setup::setup_rtc(bool enable, std::uint8_t freq_shift)
        {
            dpmi::interrupt_mask no_irq { };
            reset_rtc();
            if (!enable) return;

            if (freq_shift < 1 || freq_shift > 15) throw std::out_of_range("RTC frequency shift must be a value between 1 and 15, inclusive.");
            ns_per_rtc_tick = 1e9 / (max_rtc_frequency >> (freq_shift - 1));
            rtc_irq.set_irq(8);
            rtc_irq.enable();

            rtc_index.write(0x8B);                  // disable NMI, select register 0x0B
            auto b = rtc_data.read();               // read register
            rtc_index.write(0x8B);
            rtc_data.write(b | 0x40);               // set interrupt enable bit

            freq_shift &= 0x0F;
            rtc_index.write(0x8A);                  // disable NMI, select register 0x0A
            auto a = rtc_data.read() & 0xF0;        // read register, clear lower 4 bits
            rtc_index.write(0x8A);
            rtc_data.write(a | freq_shift);         // set freq shift bits

            rtc_index.write(0x0C);                  // enable NMI, select register 0x0C
            rtc_data.read();                        // read and discard data
        }

        void setup::setup_tsc(std::size_t sample_size, tsc_reference r)
        {
            if (sample_size != 0) tsc_max_sample_size = sample_size;
            if (r != tsc_reference::none && (r != current_tsc_ref() || current_tsc_ref() == tsc_reference::none))
            {
                preferred_tsc_ref = r;
                reset_tsc();
            }
            thread::yield_while([] { return tsc_ticks_per_irq == 0; });
        }

        void setup::reset_pit()
        {
            dpmi::interrupt_mask no_irq { };
            if (current_tsc_ref() == tsc_reference::pit) reset_tsc();
            pit_irq.disable();
            pit_ticks = 0;
            pit_cmd.write(0x34);
            pit0_data.write(0);
            pit0_data.write(0);
        }

        void setup::reset_rtc()
        {
            dpmi::interrupt_mask no_irq { };
            if (current_tsc_ref() == tsc_reference::rtc) reset_tsc();
            rtc_irq.disable();
            rtc_ticks = 0;
            rtc_index.write(0x8B);                  // disable NMI, select register 0x0B
            auto b = rtc_data.read();               // read register
            rtc_index.write(0x8B);
            rtc_data.write(b & ~0x40);              // clear interrupt enable bit
            rtc_index.write(0x0C);                  // enable NMI, select register 0x0C
            rtc_data.read();                        // read and discard data
        }

        void setup::reset_tsc()
        {
            dpmi::interrupt_mask no_irq { };
            tsc_sample_size = 0;
            tsc_total = 0;
            tsc_ticks_per_irq = 0;
            tsc_resync = true;
        }

        setup::reset_all::~reset_all()
        {
            reset_pit();
            reset_rtc();
        }

        rtc::time_point rtc::now() noexcept
        {
            dpmi::interrupt_mask no_irq { };
            auto read_bcd = []
            {
                auto bcd = setup::rtc_data.read();
                return (bcd >> 4) * 10 + (bcd & 0xF);
            };
            auto set_index = [](byte i)
            {
                setup::rtc_index.write(i);
            };

            std::uint64_t sec { 0 };
            set_index(0x80);    // second
            sec += read_bcd();
            set_index(0x82);    // minute
            sec += read_bcd() * 60;
            set_index(0x84);    // hour
            sec += read_bcd() * 60 * 60;
            set_index(0x87);    // day
            sec += (read_bcd() - 1) * 60 * 60 * 24;
            set_index(0x88);    // month
            sec += (read_bcd() - 1) * 60 * 60 * 24 * (365.2425 / 12);
            set_index(0x09);    // year
            sec += read_bcd() * 60 * 60 * 24 * 365.2425;
            sec += 946684800;   // seconds from 1970 to 2000
            sec *= static_cast<std::uint64_t>(1e6);
            sec += setup::rtc_ticks * setup::ns_per_rtc_tick / 1e3;
            return time_point { duration { sec } };
        }
    }
}
