/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

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
        interrupt_id id { i, interrupt_type::irq };
        try
        {
            std::optional<irq_mask> mask;
            auto* entry = data->get(i);
            auto flags = entry->flags;
            if (not (flags & no_interrupts)) asm("sti");
            else if (flags & no_reentry) mask.emplace(i);
            if (not (flags & no_auto_eoi)) send_eoi_without_acknowledge();

            entry->call();
        }
        catch (...)
        {
            fmt::print(stderr, "Exception while servicing IRQ {:d}\n", i);
            try { throw; }
            catch (...) { print_exception(); }
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

        asm("cli");
    }

    void irq_controller::call()
    {
        auto* id = interrupt_id::get();
        for (auto f : handler_chain)
        {
            if (f->flags & always_call or id->acknowledged != ack::yes) f->function();
        }
        if (flags & always_chain or id->acknowledged == ack::no)
        {
            interrupt_mask no_ints_here { };
            call_far_iret(prev_handler);
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

    void irq_controller::add(irq_level i, irq_handler* p)
    {
        interrupt_mask no_irqs_here { };
        if (data == nullptr) data = new irq_controller_data { };
        auto* e = data->add(i);
        e->handler_chain.push_back(p);
        e->flags |= p->flags;
        irq_mask::unmask(i);
        if (i > 7) irq_mask::unmask(2);
    }

    void irq_controller::remove(irq_level i, irq_handler* p)
    {
        interrupt_mask no_irqs_here { };
        auto* e = data->get(i);
        e->handler_chain.erase(std::remove_if(e->handler_chain.begin(), e->handler_chain.end(), [p](auto a) { return a == p; }), e->handler_chain.end());
        e->flags = { };
        for (auto* p : e->handler_chain) e->flags |= p->flags;
        if (e->handler_chain.empty()) data->remove(i);
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
