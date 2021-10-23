/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <algorithm>
#include <optional>
#include <cstddef>
#include <jw/main.h>
#include <jw/thread.h>
#include <jw/dpmi/irq_handler.h>
#include <jw/dpmi/fpu.h>
#include <jw/alloc.h>

namespace jw::dpmi::detail
{
    struct irq_data_t
    {
        selector ss;
        selector ds;
        selector es;
        selector fs;
        selector gs;
        std::uint32_t stack_use_count;
    } static irq_data;

    struct irq_entry_point
    {
        [[gnu::naked, gnu::hot]]
        static void enter() noexcept
        {
            asm
            (
                R"(
                push ds
                push es
                push fs
                push gs
                pusha
                mov bx, ss
                mov ebp, esp
                lea esi, %[data]
                mov ds, cs:[esi+%[ds]]
                mov di, [esi+%[ss]]
                mov es, [esi+%[es]]
                mov fs, [esi+%[fs]]
                mov gs, [esi+%[gs]]
                cmp bx, di
                je L%=keep_stack
                call %[get_stack]
                mov edx, [esp+0x30]
                mov ss, di
                mov esp, eax;
            L%=keep_stack:
                push edx
                call %[handle_irq]
                cmp bx, di
                je L%=ret_same_stack
                dec dword ptr [esi+%[use_count]]
                mov ss, bx
            L%=ret_same_stack:
                mov esp, ebp
                popa
                pop gs
                pop fs
                pop es
                pop ds
                add esp, 4
                sti
                iret
                )"
                ::
                [data]          "m" (irq_data),
                [ss]            "i" (offsetof(irq_data_t, ss)),
                [ds]            "i" (offsetof(irq_data_t, ds)),
                [es]            "i" (offsetof(irq_data_t, es)),
                [fs]            "i" (offsetof(irq_data_t, fs)),
                [gs]            "i" (offsetof(irq_data_t, gs)),
                [use_count]     "i" (offsetof(irq_data_t, stack_use_count)),
                [handle_irq]    "i" (irq_controller::handle_irq),
                [get_stack]     "i" (irq_controller::get_stack_ptr)
            );
        }
    };

    template<unsigned N>
    [[gnu::naked, gnu::hot]]
    static void irq_trampoline() noexcept
    {
        asm
        (
            R"(
            push %0
            jmp %1
            )"
            ::
            "i" (N),
            "i" (irq_entry_point::enter)
        );
    }

    template<>
    [[gnu::naked, gnu::hot]]
    void irq_trampoline<7>() noexcept
    {
        asm
        (
            R"(
            push eax
            mov al, 0x0b
            out 0x20, al
            in al, 0x20
            test al, 0x80
            jz L%=spurious
            mov eax, [esp]
            mov dword ptr [esp], 7
            jmp %0
        L%=spurious:
            pop eax
            sti
            iret
            )"
            : : "i" (irq_entry_point::enter)
        );
    }

    template<>
    [[gnu::naked, gnu::hot]]
    void irq_trampoline<15>() noexcept
    {
        asm
        (
            R"(
            push eax
            mov al, 0x0b
            out 0xa0, al
            in al, 0xa0
            test al, 0x80
            jz L%=spurious
            mov eax, [esp]
            mov dword ptr [esp], 15
            jmp %0
        L%=spurious:
            mov al, 0x62
            out 0x20, al
            pop eax
            sti
            iret
            )"
            : : "i" (irq_entry_point::enter)
        );
    }

    std::uintptr_t get_trampoline(irq_level i)
    {
        constexpr static auto array = []<std::size_t... I> (std::index_sequence<I...>) {
            return std::array<decltype(&irq_trampoline<0>), sizeof...(I)> { irq_trampoline<I>... };
        } (std::make_index_sequence<16> { });
        return reinterpret_cast<std::uintptr_t>(array[i]);
    }

    void irq_controller::handle_irq(irq_level i) noexcept
    {
        interrupt_id id { i, interrupt_type::irq };
        fpu_context_switcher->enter(0);

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
            std::cerr << "Exception while servicing IRQ " << std::dec << i << std::endl;
            try { throw; }
            catch (const std::exception& e) { print_exception(e); }
            catch (...) { }
            do { asm("cli; hlt"); } while (true);
        }

        std::byte* esp; asm("mov %0, esp;":"=rm"(esp));
        auto stack_left = static_cast<std::size_t>(esp - data->stack.data());
        if (stack_left <= config::interrupt_minimum_stack_size) [[unlikely]]
            if (not data->resizing_stack.test_and_set())
                this_thread::invoke_next([data = data] { data->resize_stack(data->stack.size() * 2); });

        asm("cli");
        fpu_context_switcher->leave();
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
        return data->stack.data() + (data->stack.size() >> (irq_data.stack_use_count++)) - 4;
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
        auto& d = irq_data;
        asm ("mov %0, ss" : "=rm" (d.ss));
        asm ("mov %0, ds" : "=rm" (d.ds));
        asm ("mov %0, es" : "=rm" (d.es));
        asm ("mov %0, fs" : "=rm" (d.fs));
        asm ("mov %0, gs" : "=rm" (d.gs));
        d.stack_use_count = 0;
    }
}
