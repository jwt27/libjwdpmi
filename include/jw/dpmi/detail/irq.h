/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/thread.h>
#include <jw/dpmi/detail/interrupt_id.h>
#include <jw/function.h>

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            class irq_controller;

            struct [[gnu::packed]] irq_wrapper : class_lock<irq_wrapper>
            {
                friend class irq_controller;
                using entry_fptr = void(*)(irq_level) noexcept;
                using stack_fptr = byte*(*)() noexcept;

            private:
                selector ss;                    // [esi-0x26]
                std::uint32_t* use_cnt;         // [esi-0x24]
                stack_fptr get_stack;           // [esi-0x20]
                std::uint32_t irq;              // [esi-0x1C]
                selector ds;                    // [esi-0x18]
                selector es;                    // [esi-0x16]
                selector fs;                    // [esi-0x14]
                selector gs;                    // [esi-0x12]
                entry_fptr entry_point;         // [esi-0x10]
                std::array<byte, 0x60> code;    // [esi-0x0C]

            public:
                irq_wrapper(irq_level i, entry_fptr entry_f, stack_fptr stack_f, std::uint32_t* use_cnt_ptr) noexcept;
                auto get_ptr(selector cs = get_cs()) const noexcept { return far_ptr32 { cs, reinterpret_cast<std::uintptr_t>(code.data()) }; }
            };

            struct irq_handler_base
            {
                template<typename F>
                irq_handler_base(F&& func, irq_config_flags f = { }) : handler_ptr { std::forward<F>(func) }, flags { f } { }
                irq_handler_base() = delete;

                const trivial_function<void()> handler_ptr;
                const irq_config_flags flags;
            };

            class irq_controller
            {

                std::deque<irq_handler_base*, locking_allocator<irq_handler_base*>> handler_chain { };
                irq_level irq;
                far_ptr32 old_handler { };
                irq_wrapper wrapper;
                irq_config_flags flags { };

                void add_flags() noexcept { flags = { }; for (auto* p : handler_chain) flags |= p->flags; }

                static void set_pm_interrupt_vector(int_vector v, far_ptr32 ptr);
                static far_ptr32 get_pm_interrupt_vector(int_vector v);

                INTERRUPT void call();

            public:
                irq_controller(irq_level i);
                irq_controller(irq_controller&& m) = delete;
                irq_controller(const irq_controller& m) = delete;
                irq_controller& operator=(irq_controller&& m) = delete;
                irq_controller& operator=(const irq_controller& m) = delete;

                ~irq_controller() { if (old_handler.offset != 0) set_pm_interrupt_vector(irq_to_vec(wrapper.irq), old_handler); }

                static void add(irq_level i, irq_handler_base* p);
                static void remove(irq_level i, irq_handler_base* p);

                INTERRUPT static void acknowledge() noexcept
                {
                    if (is_acknowledged()) return;
                    send_eoi(interrupt_id::get()->num);
                    interrupt_id::acknowledge();
                }

            private:
                static int_vector irq_to_vec(irq_level i) noexcept
                {
                    assume(i < 16);
                    dpmi::version ver { };
                    return i < 8 ? i + ver.pic_master_base : i - 8 + ver.pic_slave_base;
                }
                static irq_level vec_to_irq(int_vector v) noexcept
                {
                    dpmi::version ver { };
                    if (v >= ver.pic_master_base && v < ver.pic_master_base + 8u) return v - ver.pic_master_base;
                    if (v >= ver.pic_slave_base && v < ver.pic_slave_base + 8u) return v - ver.pic_slave_base + 8;
                    return 0xff;
                }

                static bool is_irq(int_vector v) { return vec_to_irq(v) != 0xff; }
                static bool is_acknowledged()
                {
                    return interrupt_id::get()->acknowledged;
                }

                INTERRUPT static auto in_service() noexcept
                {
                    split_uint16_t r;
                    pic0_cmd.write(0x0B);
                    pic1_cmd.write(0x0B);
                    r.lo = pic0_cmd.read();
                    r.hi = pic1_cmd.read();
                    return std::bitset<16> { r };
                }

                INTERRUPT static void send_eoi(irq_level i) noexcept;

                INTERRUPT static byte* get_stack_ptr() noexcept;
                INTERRUPT [[gnu::force_align_arg_pointer, gnu::cdecl]] static void interrupt_entry_point(irq_level) noexcept;

                struct irq_controller_data;

                static constexpr io::io_port<byte> pic0_cmd { 0x20 };
                static constexpr io::io_port<byte> pic1_cmd { 0xA0 };
                static inline irq_controller_data* data { nullptr };
            };

            struct irq_controller::irq_controller_data : class_lock<irq_controller_data>
            {
                irq_controller_data()
                {
                    stack.resize(config::interrupt_initial_stack_size);
                    pic0_cmd.write(0x68);   // TODO: restore to defaults
                    pic1_cmd.write(0x68);
                }

                irq_controller* get(irq_level i)
                {
                    return reinterpret_cast<irq_controller*>(&entries[i]);
                }

                irq_controller* add(irq_level i)
                {
                    auto* entry = reinterpret_cast<irq_controller*>(&entries[i]);
                    if (not allocated[i])
                    {
                        entry = new (entry) irq_controller { i };
                        allocated[i] = true;
                    }
                    return entry;
                }

                void remove(irq_level i)
                {
                    auto* entry = reinterpret_cast<irq_controller*>(&entries[i]);
                    entry->~irq_controller();
                    allocated[i] = false;
                }

                std::bitset<16> allocated { };
                std::array<std::aligned_storage_t<sizeof(irq_controller), alignof(irq_controller)>, 16> entries;
                std::vector<byte, locking_allocator<byte>> stack { };
                std::uint32_t stack_use_count { 0 };
            };

            inline irq_controller::irq_controller(irq_level i) : old_handler(get_pm_interrupt_vector(irq_to_vec(i))),
                wrapper(i, interrupt_entry_point, get_stack_ptr, &data->stack_use_count)
            {
                set_pm_interrupt_vector(irq_to_vec(wrapper.irq), wrapper.get_ptr());
            }

            inline void irq_controller::add(irq_level i, irq_handler_base* p)
            {
                interrupt_mask no_ints_here { };
                if (data == nullptr) data = new irq_controller_data { };
                auto* e = data->add(i);
                e->handler_chain.push_back(p);
                e->add_flags();
                irq_mask::unmask(i);
                if (i > 7) irq_mask::unmask(2);
            }

            inline void irq_controller::remove(irq_level i, irq_handler_base* p)
            {
                interrupt_mask no_ints_here { };
                auto* e = data->get(i);
                e->handler_chain.erase(std::remove_if(e->handler_chain.begin(), e->handler_chain.end(), [p](auto a) { return a == p; }), e->handler_chain.end());
                e->add_flags();
                if (e->handler_chain.empty()) data->remove(i);
                if (data->allocated.none())
                {
                    delete data;
                    data = nullptr;
                }
            }

            inline void irq_controller::send_eoi(irq_level i) noexcept
            {
                if (data->get(i)->flags & always_chain) return;
                auto s = in_service();

                if (i >= 8)
                {
                    if (s[i]) pic1_cmd.write((i % 8) | 0x60);
                    if (s[2]) pic0_cmd.write(0x62);
                }
                else if (s[i]) pic0_cmd.write(i | 0x60);
            }
        }
    }
}
