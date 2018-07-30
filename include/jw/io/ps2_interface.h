/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <atomic>
#include <mutex>
#include <jw/io/io_error.h>
#include <jw/io/keyboard_interface.h>
#include <jw/dpmi/irq.h>
#include <jw/dpmi/lock.h>
#include <jw/io/ioport.h>
#include <jw/thread/task.h>
#include <jw/chrono/chrono.h>

// TODO: clean this up
// TODO: keyboard commands enum, instead of using raw hex values
// TODO: save and restore keyboard settings on exit!

namespace jw
{
    namespace io
    {     
        class ps2_interface : public keyboard_interface, dpmi::class_lock<ps2_interface>    // TODO: mouse interface too.
        {
            static bool initialized;

        public:
            virtual std::deque<detail::scancode> get_scancodes() override;

            scancode_set current_scancode_set;
            virtual scancode_set get_scancode_set() override { return current_scancode_set; }
            virtual void set_scancode_set(byte set) override;

            virtual void set_typematic(byte, byte) override { /* TODO */ }

            virtual void enable_typematic(bool enable) override
            {
                if (get_scancode_set() != set3) return;
                byte cmd = enable ? 0xFA : 0xF8;
                command<send_data, recv_kb_ack>({ cmd });
            }

            keyboard_leds current_led_state;
            virtual void set_leds(keyboard_leds state) override
            {
                if (state == current_led_state) return;
                command<send_data, recv_kb_ack, send_data, recv_kb_ack>({ 0xED, state });
                current_led_state = state;
            }

            virtual void set_keyboard_update_thread(thread::task<void()> t) override
            {
                keyboard_update_thread = t;
                keyboard_update_thread->name = "Keyboard auto-update thread";
            }

            ps2_interface();
            virtual ~ps2_interface();

        private:
            void write_to_controller(byte b)
            {
                thread::yield_while([this]() { return get_status().busy; });
                command_port.write(b);
            }

            void write_to_keyboard(byte b)
            {
                thread::yield_while([this]() { return get_status().busy; });
                data_port.write(b);
            }

            byte read_from_controller()
            {
                bool timeout = thread::yield_while_for([this]
                {
                    return not get_status().data_available;
                }, std::chrono::milliseconds { 100 });
                if (timeout) throw timeout_error { "Keyboard timeout" };
                return data_port.read();
            }

            byte read_from_keyboard()
            {
                auto b = read_from_controller();
                if (config.translate_scancodes) b = detail::scancode::undo_translation(b);
                return b;
            }

            void reset();

            enum cmd_sequence_element
            {
                nop,                // no operation
                send_cmd,           // send to command (controller) port
                send_data,          // send to data (keyboard) port
                recv_discard_any,   // receive any data and discard it
                recv_kb_any,        // receive any keyboard data
                recv_kb_ack,        // receive keyboard data, expect ACK
                recv_kb_data,       // receive keyboard data, expect non-ACK
                recv_ctrl_any,      // receive any controller data
                recv_ctrl_ack,      // receive controller data, expect ACK
                recv_ctrl_data,     // receive controller data, expect non-ACK
                end_sequence        // end of sequence
            };
            
            template<cmd_sequence_element = nop> constexpr void do_ps2_command(const byte*&, byte&) { }

            template<cmd_sequence_element cmd = nop, cmd_sequence_element next = end_sequence, cmd_sequence_element... etc> 
            void ps2_command(const byte* in, byte& out)
            {
                if (cmd == end_sequence) return;
                do_ps2_command<cmd>(in, out);
                ps2_command<next, etc...>(in, out);
            }

            template<cmd_sequence_element... cmd>
            byte command(const std::initializer_list<byte>& data)
            {
                retry:
                try
                {
                    std::unique_lock<std::mutex> lock { mutex };
                    dpmi::irq_mask no_irq { 1 };
                    byte result { keyboard_response::ERROR };
                    ps2_command<cmd...>(data.begin(), result);
                    return result;
                }
                catch (const io_error&)
                {
                    reset();
                    goto retry;
                }
            }

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

            const in_port<controller_status> status_port { 0x64 };
            const out_port<byte> command_port { 0x64 };
            const io_port<byte> data_port { 0x60 };
            std::mutex mutex;

            controller_status get_status()
            {
                auto s = status_port.read();
                if (s.timeout_error) throw timeout_error { "Keyboard timeout" };
                if (s.parity_error) throw parity_error { "Keyboard parity error" };
                return s;
            }

            struct controller_configuration_data
            {
                union
                {
                    struct[[gnu::packed]]
                    {
                        bool keyboard_interrupt : 1;
                        bool mouse_interrupt : 1;
                        bool initialized : 1;
                        bool inhibit_override : 1;
                        bool disable_keyboard : 1;
                        bool disable_mouse : 1;
                        bool translate_scancodes : 1;
                        bool : 1;
                    };
                    byte data = 0;
                };
            } config, initial_config;

            void read_config() { config.data = command<send_cmd, recv_ctrl_data>({ 0x20 }); }
            void write_config() { command<send_cmd, send_data>({ 0x60, config.data }); read_config(); }

            thread::task<void()> keyboard_update_thread;

            dpmi::locked_pool_allocator<> alloc { 1_KB };
            std::deque<detail::raw_scancode, dpmi::locked_pool_allocator<>> scancode_queue { alloc };

            dpmi::irq_handler irq_handler { [this]() INTERRUPT
            {
                if (get_status().data_available)
                {
                    do
                    {
                        auto c = data_port.read();
                        if (config.translate_scancodes) *detail::scancode::undo_translation_inserter(scancode_queue) = c;
                        else scancode_queue.push_back(c);
                    } while (get_status().data_available);
                    if (keyboard_update_thread) keyboard_update_thread->start();
                    dpmi::irq_handler::acknowledge();
                }
            }, dpmi::no_auto_eoi };

            void check_data(byte b) const
            {
                switch (b)
                {
                case ACK:
                    std::cerr << "PS/2 interface: unexpected ACK!" << std::endl;
                    [[fallthrough]];
                case RESEND:
                    throw io_error("Keyboard on fire.");
                default:
                    return;
                }
            }

            void check_ack(byte b) const
            {
                switch (b)
                {
                case ACK:
                    return;
                default:
                    std::cerr << "PS/2 interface: expected ACK, got this: " << std::hex << static_cast<unsigned>(b) << std::endl;
                    [[fallthrough]];
                case RESEND:
                    throw io_error("Keyboard on fire.");
                }
            }
        };

        template<> inline void ps2_interface::do_ps2_command<ps2_interface::send_cmd>(const byte*& in, byte&) { write_to_controller(*(in++)); }
        template<> inline void ps2_interface::do_ps2_command<ps2_interface::send_data>(const byte*& in, byte&) { write_to_keyboard(*(in++)); }
        template<> inline void ps2_interface::do_ps2_command<ps2_interface::recv_discard_any>(const byte*&, byte&) { read_from_controller(); }
        template<> inline void ps2_interface::do_ps2_command<ps2_interface::recv_kb_any>(const byte*&, byte& out) { out = read_from_keyboard(); }
        template<> inline void ps2_interface::do_ps2_command<ps2_interface::recv_kb_ack>(const byte*&, byte&) { check_ack(read_from_keyboard()); }
        template<> inline void ps2_interface::do_ps2_command<ps2_interface::recv_kb_data>(const byte*&, byte& out) { check_data(out = read_from_keyboard()); }
        template<> inline void ps2_interface::do_ps2_command<ps2_interface::recv_ctrl_any>(const byte*&, byte& out) { out = read_from_controller(); }
        template<> inline void ps2_interface::do_ps2_command<ps2_interface::recv_ctrl_ack>(const byte*&, byte&) { check_ack(read_from_controller()); }
        template<> inline void ps2_interface::do_ps2_command<ps2_interface::recv_ctrl_data>(const byte*&, byte& out) { check_data(out = read_from_controller()); }
    }
}
