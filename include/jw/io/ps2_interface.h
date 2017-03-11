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

#pragma once
#include <atomic>
#include <jw/io/kb_interface.h>
#include <jw/dpmi/irq.h>
#include <jw/io/ioport.h>

namespace jw
{
    namespace io
    {
            //TODO: mouse interface too.
        class ps2_interface : public keyboard_interface
        {
            static bool initialized;

            //static std::deque<unsigned char> read_ps2_data();
            //static void write_ps2_command(std::deque<unsigned char> bytes);
        public:

            virtual std::deque<scancode> get_scancodes() override;

            scancode_set _scancode_set;
            virtual scancode_set get_scancode_set() override { return _scancode_set; }
            virtual void set_scancode_set(byte set) override;

            virtual void set_typematic(byte, byte) override { /* TODO */ }

            virtual void enable_typematic(bool enable) override
            {
                if (get_scancode_set() != set3) return;
                byte cmd = enable ? 0xFA : 0xF8;
                command({ send_data, recv_ack }, { cmd });
            }

            leds current_led_state;
            virtual void set_leds(leds state) override
            {
                if (state == current_led_state) return;
                command({ send_data, recv_ack, send_data, recv_ack }, { 0xED, state });
                current_led_state = state;
            }

            ps2_interface();
            virtual ~ps2_interface();

        private:
            void write_to_controller(byte b);
            void write_to_keyboard(byte b);
            byte read_from_keyboard();
            void reset();

            enum cmd_sequence_element
            {
                nop,
                send_cmd,
                send_data,
                recv_any,
                recv_discard_any,
                recv_ack,
                recv_data
            };
            using cmd_sequence = std::deque<cmd_sequence_element>;

            std::deque<byte> command(const cmd_sequence& sequence, const std::deque<byte>& bytes);

            struct[[gnu::packed]] controller_status
            {
                bool data_available : 1;
                bool busy : 1;
                bool initialized : 1;
                bool write_to_controller : 1;
                bool keyboard_disabled : 1;
                bool mouse_data_available : 1;
                bool timeout_error : 1;
                bool parity_error : 1;
            };

            io::in_port<controller_status> status_port { 0x64 };
            io::out_port<byte> command_port { 0x64 };
            io::io_port<byte> data_port { 0x60 };

            auto get_status() { return status_port.read(); }

            struct controller_configuration_data
            {
                union
                {
                    struct[[gnu::packed]]
                    {
                        bool keyboard_interrupt : 1;
                        bool mouse_interrupt : 1;
                        bool initialized : 1;
                        bool : 1;
                        bool keyboard_enabled : 1;
                        bool mouse_enabled : 1;
                        bool translate_scancodes : 1;
                        bool : 1;
                    };
                    byte data = 0;
                };
            } config, initial_config;

            void read_config();
            void write_config();

            dpmi::locked_pool_allocator<> alloc { 1_KB };
            std::deque<raw_scancode, dpmi::locked_pool_allocator<>> scancode_queue { alloc };

            void read_data()
            { 
                while (get_status().data_available)
                {
                    if (config.translate_scancodes) *scancode::undo_translation_inserter(scancode_queue) = data_port.read();
                    else scancode_queue.push_back(data_port.read());
                }
            }

            dpmi::irq_handler irq_handler { [this](auto* ack) INTERRUPT
            {
                if (get_status().data_available) ack();
                read_data();
                // TODO: command / ACK handling here
            } };
        };
    }
}
