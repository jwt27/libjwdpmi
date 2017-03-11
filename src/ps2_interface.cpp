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

#include <iostream>
#include <jw/io/ps2_interface.h>
#include <jw/dpmi/irq_mask.h>
#include <jw/thread/thread.h>

//#define DOSBOX_BUG

// references: 
// IBM keyboard datasheet   --> http://www.mcamafia.de/pdf/ibm_hitrc11.pdf
// Virtualbox keyboard code --> https://www.virtualbox.org/svn/vbox/trunk/src/VBox/Devices/Input/PS2K.cpp
// Dosbox-X keyboard code   --> https://github.com/joncampbell123/dosbox-x/blob/master/src/hardware/keyboard.cpp
// OSDev                    --> http://wiki.osdev.org/%228042%22_PS/2_Controller
//                          --> http://wiki.osdev.org/PS/2_Keyboard

namespace jw
{
    namespace io
    {
        bool ps2_interface::initialized;

        std::deque<detail::scancode> ps2_interface::get_scancodes()
        {
            dpmi::irq_mask disable_irq { 1 };
            return detail::scancode::extract(scancode_queue, get_scancode_set());
        }

        void ps2_interface::set_scancode_set(byte set)
        {
        #ifdef DOSBOX_BUG
            command({ send_data, recv_ack, send_data, recv_ack, recv_ack }, { 0xF0, set }); // Dosbox-X sends two ACKs
        #else
            command({ send_data, recv_ack, send_data, recv_ack }, { 0xF0, set });
        #endif

            _scancode_set = static_cast<scancode_set>(
        #ifdef DOSBOX_BUG
                command({ send_data, recv_ack, send_data, recv_data, recv_ack }, { 0xF0, 0 })[0]);  // Dosbox-X sends ACK-data-ACK...
        #else
                command({ send_data, recv_ack, send_data, recv_ack, recv_data }, { 0xF0, 0 })[0]);  // Should be ACK-ACK-data.
        #endif
        }

        void ps2_interface::reset()
        {
            irq_handler.disable();
            irq_handler.set_irq(1);
            config.translate_scancodes = false;
            config.keyboard_interrupt = true;
            write_config();

            set_scancode_set(3);
            enable_typematic(true);

            irq_handler.enable();
        }

        ps2_interface::ps2_interface()
        {
            if (initialized) throw std::exception(); // only one instance allowed

            dpmi::irq_mask irq1_disable { 1 };
            config.translate_scancodes = true;
            read_config();
            initial_config = config;

            reset();

            initialized = true;
        }

        ps2_interface::~ps2_interface()
        {
            irq_handler.disable();
            command({ send_data, recv_ack, recv_discard_any }, { 0xFF });  // reset keyboard
            config = initial_config;
            write_config();                 // restore PS/2 configuration data
        }

        void ps2_interface::read_config() { config.data = command({ send_cmd, recv_data }, { 0x20 })[0]; }
        void ps2_interface::write_config() { command({ send_cmd, send_data }, { 0x60, config.data }); read_config(); }

        inline void ps2_interface::write_to_controller(byte b)
        {
            thread::yield_while([&]() { return get_status().busy; });
            command_port.write(b);
        }

        inline void ps2_interface::write_to_keyboard(byte b)
        {
            thread::yield_while([&]() { return get_status().busy; });
            data_port.write(b);
        }

        inline byte ps2_interface::read_from_keyboard() //TODO: timeout check 
        {
            //do
            //{
            //    assert(!get_status().timeout_error);
            //    assert(!get_status().parity_error);  // TODO: throw exceptions
            //} while (!get_status().data_available);
            thread::yield_while([&]() { return !get_status().data_available; });
            //while (!get_status().data_available) thread::yield();
            auto b = data_port.read();
            if (config.translate_scancodes) b = detail::scancode::undo_translation(b);
            return b;
        }

        std::deque<byte> ps2_interface::command(const cmd_sequence& sequence, const std::deque<byte>& bytes)
        {
            byte data = 0;
            std::deque<byte> response { };
            auto cmd = bytes.begin();

            dpmi::irq_mask disable_irq { 1 }; // TODO: don't disable IRQ, let IRQ deal with response
            for (auto e = sequence.begin(); e != sequence.end(); ++e)
            {
                switch (*e)
                {
                case nop:
                    break;
                case send_cmd:
                {
                    write_to_controller(*(cmd++));
                    auto next_e = e + 1;
                    if (next_e != sequence.end() && (*next_e) == send_data)
                        //scheduler::yield_while([&]() { return !get_status().write_to_controller; });
                        while (!get_status().write_to_controller) thread::yield();
                }
                break;

                case send_data:
                    write_to_keyboard(*(cmd++));
                    break;

                case recv_any:
                    response.push_back(read_from_keyboard());
                    break;

                case recv_discard_any:
                    read_from_keyboard();
                    break;

                case recv_ack: //TODO: let ISR deal with recv
                    do
                    {
                        data = read_from_keyboard();
                        switch (data)
                        {
                        case ACK:
                            break;
                        case RESEND:
                            throw std::runtime_error("Keyboard on fire.");
                        default:
                            std::cerr << "got scancode instead of ACK? " << std::hex << (unsigned)data << std::endl;
                            scancode_queue.push_back(data); // TODO: translation
                        }
                    } while (data != ACK);
                    break;

                case recv_data:
                    auto r_size = response.size();
                    do
                    {
                        data = read_from_keyboard();
                        switch (data)
                        {
                        case ACK:
                            std::cerr << "--> unexpected ACK!" << std::endl;
                            break;
                        case RESEND:
                            throw std::runtime_error("Keyboard on fire.");
                        default:
                            response.push_back(data);
                        }
                    } while (response.size() == r_size);
                    break;
                }
            }

            return response;
        }
    }
}