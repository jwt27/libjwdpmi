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
#include <jw/dpmi/detail/stack.h>
#include <jw/chrono.h>
#include <jw/dpmi/cpuid.h>
#include <jw/alloc.h>
#include <jw/branchless.h>

namespace jw::dpmi::detail
{
    using irq_time_t = std::array<std::array<std::uint32_t, 32>, 16>;

    static constinit std::uint32_t spurious;
    static constinit bool have_rdtsc;
    static constinit std::conditional_t<config::collect_irq_stats, irq_stats_t, std::monostate> stats;
    static constinit std::conditional_t<config::collect_irq_stats, irq_time_t, std::monostate> irq_time;

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
            dec %[use_count]        # counter used by get_locked_stack()
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
            [use_count]  "m" (locked_stack_use_count),
            [handle_irq] "i" (irq_controller::handle_irq),
            [get_stack]  "i" (get_locked_stack)
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
            :
            : "i" (N)
            , "i" (irq_entry_point)
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
            inc [%1]
            pop eax
            sti
            iret )"
            :
            : "i" (irq_entry_point)
            , "m" (spurious)
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
            inc [%1]
            pop eax
            sti
            iret )"
            :
            : "i" (irq_entry_point)
            , "m" (spurious)
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
        [[maybe_unused]] chrono::tsc_count t_enter = 0;
        if constexpr (config::collect_irq_stats)
        {
            if (have_rdtsc) [[likely]]
                t_enter = chrono::rdtsc();
            ++stats.irq[i].count;
        }

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

                if ((flags & always_chain)
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

#ifndef NDEBUG
            if (in_service(i)) [[unlikely]]
            {
                fmt::print(stderr, "No EOI for IRQ {:d}\n", i);
                halt();
            }

            if (*reinterpret_cast<const std::uint32_t*>(locked_stack.data()) != 0xdeadbeef) [[unlikely]]
            {
                fmt::print(stderr, "Stack overflow handling IRQ {:d}\n", i);
                halt();
            }
#endif
        }

        if constexpr (config::collect_irq_stats) if (have_rdtsc) [[likely]]
        {
            const std::uint32_t t = chrono::rdtsc() - t_enter;
            irq_time[i][(stats.irq[i].count - 1) & (irq_time[i].size() - 1)] = t;
            stats.irq[i].min = min(stats.irq[i].min, t);
            stats.irq[i].max = max(stats.irq[i].max, t);
        }
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
        pic0_cmd.write(0x68);   // TODO: restore to defaults
        pic1_cmd.write(0x68);
        *reinterpret_cast<std::uint32_t*>(locked_stack.data()) = 0xdeadbeef;
        if constexpr (config::collect_irq_stats)
        {
            spurious = 0;
            have_rdtsc = dpmi::cpuid::feature_flags().time_stamp_counter;
            for (auto& i : stats.irq)
            {
                i.min = -1;
                i.max = 0;
            }
        }
    }
}

namespace jw::dpmi
{
    irq_stats_t irq_stats() noexcept
    {
        if constexpr (config::collect_irq_stats)
        {
            auto stats = detail::stats;
            for (unsigned i = 0; i != 16; ++i)
            {
                if (stats.irq[i].count != 0)
                {
                    const auto n = std::min<unsigned>(stats.irq[i].count, detail::irq_time[i].size());
                    std::uint64_t sum = 0;
                    for (unsigned j = 0; j != n; ++ j)
                        sum += detail::irq_time[i][j];
                    stats.irq[i].avg = sum / n;
                }
                else
                {
                    stats.irq[i].avg = 0;
                    stats.irq[i].min = 0;
                }
            }
            stats.spurious = detail::spurious;
            return stats;
        }
        else return { };
    }
}
