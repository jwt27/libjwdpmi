/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <iostream>
#include <jw/io/ps2_interface.h>
#include <jw/dpmi/irq_mask.h>
#include <jw/thread/thread.h>
#include <../jwdpmi_config.h>

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
        bool ps2_interface::initialized { false };

        std::deque<detail::scancode> ps2_interface::get_scancodes()
        {
            dpmi::irq_mask disable_irq { 1 };
            return detail::scancode::extract(scancode_queue, get_scancode_set());
        }

        void ps2_interface::reset()
        {
            irq_handler.disable();
            irq_handler.set_irq(1);
            config.translate_scancodes = false;
            config.keyboard_interrupt = true;
            write_config();

            set_scancode_set(set2);
            enable_typematic(true);

            irq_handler.enable();
        }

        ps2_interface::ps2_interface()
        {
            if (initialized) throw std::runtime_error("Only one ps2_interface instance allowed.");

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
            set_scancode_set(set2);
            config = initial_config;
            config.translate_scancodes = true;
            write_config();                 // restore PS/2 configuration data
            initialized = false;
        }

        void ps2_interface::set_scancode_set(byte set)
        {
            if (config::dosbox)
            {
                command<send_data, recv_kb_ack, send_data, recv_kb_ack, recv_kb_ack>({ 0xF0, set }); // Dosbox-X sends two ACKs
                current_scancode_set = static_cast<scancode_set>(command<send_data, recv_kb_ack, send_data, recv_kb_data, recv_kb_ack>({ 0xF0, 0 }));  // Dosbox-X sends ACK-data-ACK...
            }
            else
            {
                command<send_data, recv_kb_ack, send_data, recv_kb_ack>({ 0xF0, set });
                current_scancode_set = static_cast<scancode_set>(command<send_data, recv_kb_ack, send_data, recv_kb_ack, recv_kb_data>({ 0xF0, 0 }));  // Should be ACK-ACK-data.
            }

            if (set == set3) command<send_data, recv_kb_ack>({ 0xF8 });    // Enable make/break mode for all keys.
        }
    }
}
