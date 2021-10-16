/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/thread/thread.h>
#include <jw/dpmi/detail/interrupt_id.h>
#include <jw/function.h>

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            class [[gnu::packed]] irq_wrapper : class_lock<irq_wrapper>
            {
            public:
                using entry_fptr = void(*)(int_vector) noexcept;
                using stack_fptr = byte*(*)() noexcept;

            private:
                selector ss;                    // [esi-0x26]
                std::uint32_t* use_cnt;         // [esi-0x24]
                stack_fptr get_stack;           // [esi-0x20]
                int_vector vec;                 // [esi-0x1C]
                selector ds;                    // [esi-0x18]
                selector es;                    // [esi-0x16]
                selector fs;                    // [esi-0x14]
                selector gs;                    // [esi-0x12]
                entry_fptr entry_point;         // [esi-0x10]
                std::array<byte, 0x60> code;    // [esi-0x0C]

            public:
                irq_wrapper(int_vector _vec, entry_fptr entry_f, stack_fptr stack_f, std::uint32_t* use_cnt_ptr) noexcept;
                auto get_ptr(selector cs = get_cs()) const noexcept { return far_ptr32 { cs, reinterpret_cast<std::uintptr_t>(code.data()) }; }
            };

            struct irq_handler_base
            {
                template<typename F>
                irq_handler_base(F&& func, irq_config_flags f = { }) : handler_ptr { std::forward<F>(func) }, flags { f } { }
                irq_handler_base() = delete;

                const function<void()> handler_ptr;
                const irq_config_flags flags;
            };

            class irq_controller
            {

                std::deque<irq_handler_base*, locking_allocator<irq_handler_base*>> handler_chain { };
                int_vector vec;
                far_ptr32 old_handler { };
                irq_wrapper wrapper;
                irq_config_flags flags { };

                void add_flags() noexcept { flags = { }; for (auto* p : handler_chain) flags |= p->flags; }

                static void set_pm_interrupt_vector(int_vector v, far_ptr32 ptr);
                static far_ptr32 get_pm_interrupt_vector(int_vector v);

                INTERRUPT void call();

            public:
                irq_controller(int_vector v) : vec(v), old_handler(get_pm_interrupt_vector(v)),
                    wrapper(v, interrupt_entry_point, get_stack_ptr, &data->stack_use_count)
                {
                    set_pm_interrupt_vector(vec, wrapper.get_ptr());
                }

                irq_controller(irq_controller&& m) = delete;
                irq_controller(const irq_controller& m) = delete;
                irq_controller& operator=(irq_controller&& m) = delete;
                irq_controller& operator=(const irq_controller& m) = delete;

                ~irq_controller() { if (old_handler.offset != 0) set_pm_interrupt_vector(vec, old_handler); }

                void add(irq_handler_base* p)
                {
                    interrupt_mask no_ints_here { };
                    handler_chain.push_back(p);
                    add_flags();
                    if (is_irq(vec))
                    {
                        auto i = vec_to_irq(vec);
                        irq_mask::unmask(i);
                        if (i > 7) irq_mask::unmask(2);
                    }
                    ++detail::interrupt_id::get()->use_count;
                }

                void remove(irq_handler_base* p)
                {
                    interrupt_mask no_ints_here { };
                    handler_chain.erase(std::remove_if(handler_chain.begin(), handler_chain.end(), [p](auto a) { return a == p; }), handler_chain.end());
                    add_flags();
                    if (handler_chain.empty()) data->entries.erase(vec);
                    if (data->entries.empty())
                    {
                        delete data;
                        data = nullptr;
                    }
                    --detail::interrupt_id::get()->use_count;
                    detail::interrupt_id::delete_if_possible();
                }

                struct irq_controller_data : class_lock<irq_controller_data>
                {
                    irq_controller_data()
                    {
                        stack.resize(config::interrupt_initial_stack_size);
                        pic0_cmd.write(0x68);   // TODO: restore to defaults
                        pic1_cmd.write(0x68);
                    }

                    std::map<int_vector, std::unique_ptr<irq_controller>, std::less<int_vector>, locking_allocator<std::pair<const int_vector, std::unique_ptr<irq_controller>>>> entries { };
                    std::vector<byte, locking_allocator<byte>> stack { };
                    std::uint32_t stack_use_count { 0 };
                };

                static irq_controller& get(int_vector v)
                {
                    if (data == nullptr) data = new irq_controller_data { };
                    if (data->entries.count(v) == 0) data->entries.emplace(v, std::make_unique<irq_controller>(v));
                    return *data->entries.at(v);
                }

                static irq_controller& get_irq(irq_level i) { return get(irq_to_vec(i)); }

                INTERRUPT static void acknowledge() noexcept
                {
                    if (is_acknowledged()) return;
                    send_eoi();
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
                    if (auto id = interrupt_id::get_current_interrupt().lock()) return id->acknowledged;
                    return true;
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

                INTERRUPT static void send_eoi() noexcept
                {
                    auto v = interrupt_id::get_current_interrupt().lock()->vector;
                    if (data->entries.at(v)->flags & always_chain) return;
                    auto i = vec_to_irq(v);
                    auto s = in_service();

                    if (i >= 8)
                    {
                        if (s[i]) pic1_cmd.write((i % 8) | 0x60);
                        if (s[2]) pic0_cmd.write(0x62);
                    }
                    else if (s[i]) pic0_cmd.write(i | 0x60);
                }

                INTERRUPT static byte* get_stack_ptr() noexcept;
                INTERRUPT [[gnu::force_align_arg_pointer, gnu::cdecl]] static void interrupt_entry_point(int_vector vec) noexcept;

                static constexpr io::io_port<byte> pic0_cmd { 0x20 };
                static constexpr io::io_port<byte> pic1_cmd { 0xA0 };
                static inline irq_controller_data* data { nullptr };
            };
        }
    }
}
