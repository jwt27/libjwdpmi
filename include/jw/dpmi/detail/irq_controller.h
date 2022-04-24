/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <bitset>
#include <deque>
#include <atomic>
#include <jw/function.h>
#include <jw/io/ioport.h>
#include <jw/dpmi/lock.h>
#include <jw/dpmi/alloc.h>
#include <jw/dpmi/irq_mask.h>
#include <jw/dpmi/irq_config_flags.h>

namespace jw::dpmi::detail
{
    [[gnu::naked, gnu::hot]]
    void irq_entry_point() noexcept;

    struct irq_handler_data
    {
        template<typename F>
        irq_handler_data(F&& func, irq_config_flags fl, irq_level i)
            : function { std::forward<F>(func) }, flags { fl }, irq { i } { }

        const trivial_function<void()> function;
        const irq_config_flags flags;
        irq_level irq;
        bool enabled { false };
    };

    struct irq_controller
    {
        friend void irq_entry_point() noexcept;

        static void add(const irq_handler_data* p);
        static void remove(const irq_handler_data* p);

        static void acknowledge() noexcept
        {
            auto* id = interrupt_id::get();
            if (id->acknowledged == ack::no)
                send_eoi(id->num);
            id->acknowledged = ack::yes;
        }

        static void set_pm_interrupt_vector(int_vector v, far_ptr32 ptr);
        static far_ptr32 get_pm_interrupt_vector(int_vector v);

    private:
        static int_vector irq_to_vec(irq_level i) noexcept
        {
            dpmi::version ver { };
            return i < 8 ? i + ver.pic_master_base : i - 8 + ver.pic_slave_base;
        }
        static irq_level vec_to_irq(int_vector v) noexcept
        {
            dpmi::version ver { };
            if (v >= ver.pic_master_base and v < ver.pic_master_base + 8u) return v - ver.pic_master_base;
            if (v >= ver.pic_slave_base and v < ver.pic_slave_base + 8u) return v - ver.pic_slave_base + 8;
            return 0xff;
        }

        static bool is_irq(int_vector v) { return vec_to_irq(v) != 0xff; }

        static std::bitset<16> in_service() noexcept
        {
            split_uint16_t r;
            pic0_cmd.write(0x0B);
            pic1_cmd.write(0x0B);
            r.lo = pic0_cmd.read();
            r.hi = pic1_cmd.read();
            return { r };
        }

        static bool in_service(irq_level i) noexcept
        {
            if (i > 8)
            {
                pic1_cmd.write(0x0B);
                return std::bitset<8> { pic1_cmd.read() }[i - 8];
            }
            pic0_cmd.write(0x0B);
            return std::bitset<8> { pic0_cmd.read() } [i] ;
        }

        static void send_eoi_without_acknowledge()
        {
            auto* id = interrupt_id::get();
            if (id->acknowledged != ack::no) return;
            send_eoi(id->num);
            id->acknowledged = ack::eoi_sent;
        }

        static void send_eoi(irq_level i) noexcept;

        [[gnu::cdecl, gnu::hot]]
        static std::byte* get_stack_ptr() noexcept;
        [[gnu::force_align_arg_pointer, gnu::cdecl, gnu::hot]]
        static void handle_irq(irq_level) noexcept;

        irq_controller(irq_level i);
        irq_controller(irq_controller&& m) = delete;
        irq_controller(const irq_controller& m) = delete;
        irq_controller& operator=(irq_controller&& m) = delete;
        irq_controller& operator=(const irq_controller& m) = delete;
        ~irq_controller();

        [[gnu::hot]] void call();

        std::deque<const irq_handler_data*, locking_allocator<const irq_handler_data*>> handler_chain { };
        const irq_level irq;
        const far_ptr32 prev_handler { };
        irq_config_flags flags { };

        struct irq_controller_data;
        static constexpr io::io_port<byte> pic0_cmd { 0x20 };
        static constexpr io::io_port<byte> pic1_cmd { 0xA0 };
        static inline irq_controller_data* data { nullptr };
    };

    struct irq_controller::irq_controller_data
    {
        irq_controller_data();
        ~irq_controller_data();

        irq_controller* get(irq_level i)
        {
            return reinterpret_cast<irq_controller*>(&entries[i]);
        }

        irq_controller* add(irq_level i)
        {
            auto* entry = get(i);
            if (not allocated[i])
            {
                entry = new (entry) irq_controller { i };
                allocated[i] = true;
            }
            return entry;
        }

        void remove(irq_level i)
        {
            auto* entry = get(i);
            entry->~irq_controller();
            allocated[i] = false;
        }

        void free_stack()
        {
            locking_allocator<std::byte> alloc { };
            if (stack.data() != nullptr) alloc.deallocate(stack.data(), stack.size());
            stack = { };
        }

        void resize_stack(std::size_t size)
        {
            interrupt_mask no_irqs { };
            locking_allocator<std::byte> alloc { };
            free_stack();
            stack = { alloc.allocate(size), size };
            resizing_stack.clear();
        }

        std::bitset<16> allocated { };
        std::array<std::aligned_storage_t<sizeof(irq_controller), alignof(irq_controller)>, 16> entries;
        std::span<std::byte> stack { };
        std::atomic_flag resizing_stack { false };
    };

    inline void irq_controller::send_eoi(irq_level i) noexcept
    {
        if (data->get(i)->flags & always_chain) return;
        if (i >= 8)
        {
            pic1_cmd.write((i % 8) | 0x60);
            pic0_cmd.write(0x62);
        }
        else pic0_cmd.write(i | 0x60);
    }
}
