/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2017 - 2024 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <bitset>
#include <atomic>
#include <jw/function.h>
#include <jw/io/ioport.h>
#include <jw/dpmi/lock.h>
#include <jw/dpmi/alloc.h>
#include <jw/dpmi/irq_mask.h>
#include <jw/dpmi/irq_config_flags.h>
#include <jw/uninitialized_storage.h>

namespace jw::dpmi::detail
{
    [[gnu::naked, gnu::hot]]
    void irq_entry_point() noexcept;

    struct irq_handler_data
    {
        friend struct irq_controller;

        template<typename F>
        irq_handler_data(F&& func, irq_config_flags fl)
            : flags { fl }
        {
            set_func(std::forward<F>(func));
        }

        irq_level assigned_irq() const { return irq; }
        bool is_enabled() const { return enabled; }

        template<typename F>
        void set_func(F&& func)
        {
            call = [this, func = std::make_tuple(std::forward<F>(func))] [[gnu::hot]]
            {
                if (enabled) [[likely]]
                    std::get<0>(func)();
                if (next) [[likely]]
                    next->call();
            };
        }

    private:

        function<void(), 4> call;
        const irq_config_flags flags;
        irq_level irq { 16 };
        bool enabled { false };
        irq_handler_data* next { nullptr };
        irq_handler_data* prev { nullptr };
    };

    struct irq_controller
    {
        friend void irq_entry_point() noexcept;

        static void enable (irq_handler_data*);
        static void disable(irq_handler_data*);

        static void assign(irq_handler_data*, irq_level);
        static void remove(irq_handler_data*);

        template<std::uint8_t irq>
        static void acknowledge() noexcept
        {
            auto* id = interrupt_id::get();
            acknowledge(id, irq);
        }

        static void acknowledge() noexcept
        {
            auto* id = interrupt_id::get();
            acknowledge(id, id->num);
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
            return std::bitset<8> { pic0_cmd.read() }[i];
        }

        static void acknowledge(interrupt_id_data*, std::uint8_t) noexcept;

        static void send_eoi(irq_level i) noexcept
        {
            if (i < 8)
            {
                pic0_cmd.write(i | 0x60);
            }
            else
            {
                pic1_cmd.write((i & 7) | 0x60);
                pic0_cmd.write(0x62);
            }
        }

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

        irq_handler_data* first { nullptr };
        irq_handler_data* last { nullptr };
        irq_handler_data* fallback { nullptr };
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
            return entries[i].pointer();
        }

        irq_controller* add(irq_level i)
        {
            if (not allocated[i])
            {
                auto* entry = new (entries[i].storage) irq_controller { i };
                allocated[i] = true;
                return entry;
            }
            else return get(i);
        }

        void remove(irq_level i)
        {
            get(i)->~irq_controller();
            allocated[i] = false;
        }

        void free_stack()
        {
            locking_allocator<std::byte> alloc { };
            if (stack.data() != nullptr)
                alloc.deallocate(stack.data(), stack.size());
            stack = { };
        }

        void resize_stack(std::size_t size)
        {
            interrupt_mask no_irqs { };
            locking_allocator<std::byte> alloc { };
            auto* const p = alloc.allocate(size);
            free_stack();
            stack = { p, size };
            resizing_stack.clear();
        }

        std::bitset<16> allocated { };
        std::array<uninitialized_storage<irq_controller>, 16> entries;
        std::span<std::byte> stack { };
        std::atomic_flag resizing_stack { false };
    };

    inline void irq_controller::acknowledge(interrupt_id_data* id, std::uint8_t irq) noexcept
    {
        if (not (data->get(irq)->flags & (late_eoi | always_chain))
            and id->acknowledged == ack::no)
            send_eoi(irq);
        id->acknowledged = ack::yes;
    }
}
