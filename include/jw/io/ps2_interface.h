/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <atomic>
#include <mutex>
#include <jw/io/detail/scancode.h>
#include <jw/io/io_error.h>
#include <jw/dpmi/irq.h>
#include <jw/dpmi/lock.h>
#include <jw/io/ioport.h>
#include <jw/thread/task.h>
#include <jw/chrono/chrono.h>

// TODO: clean this up
// TODO: keyboard commands enum, instead of using raw hex values
// TODO: mouse interface too.

namespace jw
{
    namespace io
    {     
        enum keyboard_leds : byte
        {
            scroll_lock_led = 0b001,
            num_lock_led = 0b010,
            caps_lock_led = 0b100
        };

        struct ps2_interface : dpmi::class_lock<ps2_interface>
        {
            std::deque<detail::scancode> get_scancodes();

            scancode_set current_scancode_set;
            scancode_set get_scancode_set() { return current_scancode_set; }
            void set_scancode_set(byte set) ;

            void set_typematic(byte, byte) { /* TODO */ }

            void enable_typematic(bool enable)
            {
                if (get_scancode_set() != set3) return;
                byte cmd = enable ? 0xFA : 0xF8;
                command<send_data, recv_kb_ack>({ cmd });
            }

            keyboard_leds current_led_state;
            void set_leds(bool num, bool caps, bool scroll)
            {
                set_leds(static_cast<keyboard_leds>(
                    (num ? keyboard_leds::num_lock_led : 0) |
                    (caps ? keyboard_leds::caps_lock_led : 0) |
                    (scroll ? keyboard_leds::scroll_lock_led : 0)));
            }

            void set_leds(keyboard_leds state)
            {
                if (state == current_led_state) return;
                command<send_data, recv_kb_ack, send_data, recv_kb_ack>({ 0xED, state });
                current_led_state = state;
            }

            void set_keyboard_update_thread(thread::task<void()> t)
            {
                keyboard_update_thread = t;
                keyboard_update_thread->name = "Keyboard auto-update thread";
            }

            void init_keyboard();
            void reset_keyboard();

            static auto& instance()
            {
                if (not instance_ptr) instance_ptr.reset(new ps2_interface { });
                return instance_ptr;
            }
            virtual ~ps2_interface();

        private:
            static inline std::unique_ptr<ps2_interface> instance_ptr;
            ps2_interface();

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

            enum keyboard_response
            {
                ACK = 0xFA,
                RESEND = 0xFE,
                ERROR = 0xFC
            };

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
            };

            template<cmd_sequence_element cmd, cmd_sequence_element... next>
            void do_ps2_command(const byte* in, byte& out)
            {
                [[maybe_unused]] auto check_data = [](byte b)
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
                };

                [[maybe_unused]] auto check_ack = [](byte b)
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
                };

                if constexpr (cmd == send_cmd) write_to_controller(*(in++));
                if constexpr (cmd == send_data) write_to_keyboard(*(in++));
                if constexpr (cmd == recv_discard_any) read_from_controller();
                if constexpr (cmd == recv_kb_any) out = read_from_keyboard();
                if constexpr (cmd == recv_kb_ack) check_ack(read_from_keyboard());
                if constexpr (cmd == recv_kb_data) check_data(out = read_from_keyboard());
                if constexpr (cmd == recv_ctrl_any) out = read_from_controller();
                if constexpr (cmd == recv_ctrl_ack) check_ack(read_from_controller());
                if constexpr (cmd == recv_ctrl_data) check_data(out = read_from_controller());
                if constexpr (sizeof...(next) > 0) do_ps2_command<next...>(in, out);
            }

            template<cmd_sequence_element... seq>
            byte command(const std::initializer_list<byte>& data)
            {
                retry:
                try
                {
                    std::unique_lock<std::mutex> lock { mutex };
                    dpmi::irq_mask no_irq { 1 };
                    byte result { keyboard_response::ERROR };
                    do_ps2_command<seq...>(data.begin(), result);
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
            inline static bool keyboard_initialized { false };

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
                    if (keyboard_update_thread)
                    {
                        if (keyboard_update_thread->is_running())
                        {
                            thread::invoke_main([this]
                            {
                                if (scancode_queue.size() == 0) return;
                                keyboard_update_thread->try_await();
                                keyboard_update_thread->start();
                            });
                        }
                        else keyboard_update_thread->start();
                    }
                    dpmi::irq_handler::acknowledge();
                }
            }, dpmi::no_auto_eoi };
        };
    }
}
