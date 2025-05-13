/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2017 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#include <algorithm>
#include <optional>
#include <cstddef>
#include <fmt/core.h>
#include <jw/main.h>
#include <jw/thread.h>
#include <jw/dpmi/irq_handler.h>
#include <jw/dpmi/detail/selectors.h>
#include <jw/alloc.h>

namespace jw::dpmi::detail
{
    static constinit std::uint32_t stack_use_count;

    void irq_entry_point() noexcept
    {
        asm
        (R"(
            push ds
            push es
            push fs
            push gs
            pusha
            mov ebx, ss
            mov edi, cs:[%[ds]]
            mov ebp, esp
            mov fs, ebx
            mov ds, edi
            mov es, edi
            cmp bx, di              # check if we're already on our own SS,
            je L%=keep_stack        # this is usually the case for nested IRQs
            call %[get_stack]       # get a new stack pointer in EAX
            mov ss, edi
            mov esp, eax
        L%=keep_stack:
            push fs:[ebp+0x30]      # IRQ number set by trampoline
            call %[handle_irq]      # call user interrupt handlers
            cmp bx, di
            je L%=ret_same_stack
            dec %[use_count]        # counter used by get_stack_ptr
            mov ss, ebx
        L%=ret_same_stack:
            mov esp, ebp
            popa
            pop gs
            pop fs
            pop es
            pop ds
            add esp, 4              # pop IRQ number
            sti
            iret
        )" : :
            [ds]         "i" (&safe_ds),
            [use_count]  "m" (stack_use_count),
            [handle_irq] "i" (irq_controller::handle_irq),
            [get_stack]  "i" (irq_controller::get_stack_ptr)
        );
    }

    template<unsigned N>
    [[gnu::naked, gnu::hot]]
    static void irq_trampoline() noexcept
    {
        asm
        (
        R"( push %0     # IRQ number
            jmp %1 )"
            ::
            "i" (N),
            "i" (irq_entry_point)
        );
    }

    template<>
    [[gnu::naked, gnu::hot]]
    void irq_trampoline<7>() noexcept
    {
        asm
        (
        R"( push eax
            mov al, 0x0b    # PIC in-service register
            out 0x20, al    # PIC master
            in al, 0x20
            test al, 0x80   # check for spurious interrupt
            jz L%=spurious
            pop eax
            push 7
            jmp %0
        L%=spurious:
            pop eax
            sti
            iret )"
            : : "i" (irq_entry_point)
        );
    }

    template<>
    [[gnu::naked, gnu::hot]]
    void irq_trampoline<15>() noexcept
    {
        asm
        (
        R"( push eax
            mov al, 0x0b    # in-service register
            out 0xa0, al    # PIC slave
            in al, 0xa0
            test al, 0x80   # check for spurious interrupt
            jz L%=spurious
            pop eax
            push 15
            jmp %0
        L%=spurious:
            mov al, 0x62    # ACK irq 2 on master
            out 0x20, al
            pop eax
            sti
            iret )"
            : : "i" (irq_entry_point)
        );
    }

    static std::uintptr_t get_trampoline(irq_level i)
    {
        constexpr static auto array = []<std::size_t... I> (std::index_sequence<I...>) {
            return std::array<decltype(&irq_trampoline<0>), sizeof...(I)> { irq_trampoline<I>... };
        } (std::make_index_sequence<16> { });
        return reinterpret_cast<std::uintptr_t>(array[i]);
    }

    void irq_controller::handle_irq(irq_level i) noexcept
    {
        constexpr bool save_fpu { config::save_fpu_on_interrupt };
        std::conditional_t<save_fpu, fpu_context, empty> fpu;
        interrupt_id id { &fpu, i, interrupt_type::irq };

        try
        {
            auto* const entry = data->get(i);
            const auto flags = entry->flags;
            if (not (flags & (always_chain | no_auto_eoi | late_eoi)))
            {
                send_eoi(i);
                id.acknowledged = ack::eoi_sent;
            }

            {
                std::optional<irq_mask> mask;
                finally cli { [] { asm ("cli"); } };
                if (not (flags & no_interrupts)) asm ("sti");
                else if (flags & no_reentry) mask.emplace(i);

                entry->first->call();

                if (((flags & fallback_handler) != 0)
                    & (id.acknowledged != ack::yes))
                    entry->fallback->call();
            }

            if (((flags & always_chain) != 0)
                | (id.acknowledged == ack::no)) [[unlikely]]
            {
                call_far_iret(entry->prev_handler);
                id.acknowledged = ack::eoi_sent;
            }
            else if (flags & late_eoi)
            {
                send_eoi(i);
            }
        }
        catch (...)
        {
            fmt::print(stderr, "Exception while servicing IRQ {:d}\n", i);
            try { print_exception(); }
            catch (...) { halt(); }
            halt();
        }

#       ifndef NDEBUG
        if (in_service(i))
        {
            fmt::print(stderr, "no EOI for IRQ {:d}\n", i);
            halt();
        }
#       endif

        if constexpr (config::interrupt_minimum_stack_size > 0)
        {
            std::byte* esp; asm("mov %0, esp;":"=rm"(esp));
            auto stack_left = static_cast<std::size_t>(esp - data->stack.data());
            if (stack_left <= config::interrupt_minimum_stack_size) [[unlikely]]
                if (not data->resizing_stack.test_and_set())
                    this_thread::invoke_next([data = data] { data->resize_stack(data->stack.size() * 2); });
        }
    }

    std::byte* irq_controller::get_stack_ptr() noexcept
    {
        return data->stack.data() + (data->stack.size() >> (stack_use_count++)) - 4;
    }

    void irq_controller::set_pm_interrupt_vector(int_vector v, far_ptr32 ptr)
    {
        dpmi_error_code error;
        bool c;
        asm volatile (
            "int 0x31;"
            : "=@ccc" (c)
            , "=a" (error)
            : "a" (0x0205)
            , "b" (v)
            , "c" (ptr.segment)
            , "d" (ptr.offset));
        if (c) [[unlikely]] throw dpmi_error(error, __PRETTY_FUNCTION__);
    }

    far_ptr32 irq_controller::get_pm_interrupt_vector(int_vector v)
    {
        dpmi_error_code error;
        far_ptr32 ptr;
        bool c;
        asm("int 0x31;"
            : "=@ccc" (c)
            , "=a" (error)
            , "=c" (ptr.segment)
            , "=d" (ptr.offset)
            : "a" (0x0204)
            , "b" (v));
        if (c) [[unlikely]] throw dpmi_error(error, __PRETTY_FUNCTION__);
        return ptr;
    }

    irq_controller::irq_controller(irq_level i)
        : irq { i }, prev_handler { get_pm_interrupt_vector(irq_to_vec(i)) }
    {
        const auto p = far_ptr32 { get_cs(), get_trampoline(irq) };
        set_pm_interrupt_vector(irq_to_vec(irq), p);
    }

    irq_controller::~irq_controller()
    {
        set_pm_interrupt_vector(irq_to_vec(irq), prev_handler);
    }

    void irq_controller::enable(irq_handler_data* p)
    {
        if (p->enabled) return;
        const auto i = p->irq;
        if (i >= 16) return;
        auto* e = data->get(i);
        interrupt_mask no_irqs_here { };
        p->enabled = true;
        e->flags |= p->flags;
        irq_mask::unmask(i);
        if (i > 7) irq_mask::unmask(2);
    }

    void irq_controller::disable(irq_handler_data* p)
    {
        if (not p->enabled) return;
        const auto i = p->irq;
        auto* e = data->get(i);
        interrupt_mask no_irqs_here { };
        p->enabled = false;
        e->flags = { };
        for (auto* i = e->first; i != nullptr; i = i->next)
            if (i->enabled) e->flags |= i->flags;
        if (e->fallback and e->fallback->enabled)
            e->flags |= e->fallback->flags;
    }

    void irq_controller::assign(irq_handler_data* p, irq_level i)
    {
        if (p->irq == i) return;
        remove(p);
        if (data == nullptr) data = new (locked) irq_controller_data { };
        auto* const e = data->add(i);
        const bool enabled = p->enabled;
        interrupt_mask no_irqs_here { };
        p->irq = i;
        if (not (p->flags & fallback_handler))
        {
            if (e->first == nullptr) e->first = p;
            if (e->last != nullptr) e->last->next = p;
            p->prev = e->last;
            e->last = p;
        }
        else
        {
            if (e->fallback != nullptr)
                throw std::runtime_error { fmt::format("Multiple devices registered with fallback_handler on IRQ {}", i) };
            e->fallback = p;
        }
        if (enabled) enable(p);
    }

    void irq_controller::remove(irq_handler_data* p)
    {
        const auto i = p->irq;
        if (i >= 16) return;
        auto* const e = data->get(i);
        interrupt_mask no_irqs_here { };
        disable(p);
        if (not (p->flags & fallback_handler))
        {
            if (e->first == p) e->first = p->next;
            if (e->last == p) e->last = p->prev;
            if (p->prev != nullptr) p->prev->next = p->next;
            if (p->next != nullptr) p->next->prev = p->prev;
        }
        else e->fallback = nullptr;
        p->prev = p->next = nullptr;
        p->irq = 16;

        if (e->first == nullptr) data->remove(i);
        if (data->allocated.none())
        {
            delete data;
            data = nullptr;
        }
    }

    irq_controller::irq_controller_data::irq_controller_data()
    {
        resize_stack(config::interrupt_initial_stack_size);
        pic0_cmd.write(0x68);   // TODO: restore to defaults
        pic1_cmd.write(0x68);
        stack_use_count = 0;
    }

    irq_controller::irq_controller_data::~irq_controller_data()
    {
        free_stack();
    }
}
