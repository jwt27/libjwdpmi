/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#include <algorithm>
#include <memory_resource>
#include <fmt/format.h>
#include <jw/main.h>
#include <jw/dpmi/irq_mask.h>
#include <jw/detail/scheduler.h>
#include <jw/thread.h>
#include <jw/debug/debug.h>
#include <jw/debug/detail/signals.h>
#include <jw/dpmi/realmode.h>
#ifdef JWDPMI_WITH_WATT32
# include <tcp.h>
#endif

extern "C" void __wrap___dpmi_yield()
{
    jw::this_thread::yield();
    errno = 0;
}

namespace jw::detail
{
    static constinit std::optional<dpmi::realmode_interrupt_handler> int2f_handler { std::nullopt };

    void scheduler::setup()
    {
        memres.emplace(64_KB);
        thread_allocator<thread> alloc { &*memres };
        allocator_delete<thread_allocator<thread>> deleter { alloc };
        auto* const p = new (alloc.allocate(1)) thread { };
        main_thread = std::shared_ptr<thread> { p, deleter, alloc };
        p->state = thread::running;
        p->set_name("Main thread");
        debug::throw_assert(p->id == thread::main_thread_id);
        threads.emplace(&*memres);
        threads->emplace(p->id, main_thread);
        iterator.emplace(threads->begin());

        int2f_handler.emplace(0x2f, [](dpmi::realmode_registers* reg, dpmi::far_ptr32)
        {
            if (reg->ax != 0x1680) return false;
            if (dpmi::in_irq_context()) return false;
            yield();
            errno = 0;
            reg->al = 0;
            return true;
        });
#       ifdef JWDPMI_WITH_WATT32
        sock_yield(nullptr, yield);
#       endif
    }

    void scheduler::kill_all()
    {
        int2f_handler.reset();
        atexit(main_thread.get());
        if (threads->size() == 1) [[likely]] return;
        fmt::print(stderr, "Warning: exiting with active threads.\n");
        auto thread_queue_copy = *threads;
        thread_queue_copy.erase(thread::main_thread_id);
        for (auto& t : thread_queue_copy) t.second->abort();
        main_thread->state = thread::running;
        for (auto& t : thread_queue_copy)
        {
            while (t.second->active())
            {
                try { this_thread::yield(); }
                catch (const jw::terminate_exception& e) { e.defuse(); }
            }
        }
    }

    // Save the current task context, switch to a new task, and restore its context.
    void scheduler::context_switch(thread_context**)
    {   // &current_thread->context is a parameter so the shared_ptr access is resolved on the call site
        asm volatile
        (
            "                   .cfi_def_cfa esp, 4; .cfi_rel_offset eip, 0;"
            "push ebp;          .cfi_adjust_cfa_offset 4; .cfi_rel_offset ebp, 0;"
            "push edi;          .cfi_adjust_cfa_offset 4; .cfi_rel_offset edi, 0;"
            "push esi;          .cfi_adjust_cfa_offset 4; .cfi_rel_offset esi, 0;"
            "push ebx;          .cfi_adjust_cfa_offset 4; .cfi_rel_offset ebx, 0;"
            "pushf;             .cfi_adjust_cfa_offset 4; .cfi_rel_offset eflags, 0;"
            "push fs;           .cfi_adjust_cfa_offset 4; .cfi_rel_offset fs, 0;"
            "push gs;           .cfi_adjust_cfa_offset 4; .cfi_rel_offset gs, 0;"
            "mov eax, [esp+0x20];"
            "mov [eax], esp;"
            "call %0;"
            "mov esp, eax;"
            "pop gs;            .cfi_restore gs;     .cfi_adjust_cfa_offset -4;"
            "pop fs;            .cfi_restore fs;     .cfi_adjust_cfa_offset -4;"
            "popf;              .cfi_restore eflags; .cfi_adjust_cfa_offset -4;"
            "pop ebx;           .cfi_restore ebx;    .cfi_adjust_cfa_offset -4;"
            "pop esi;           .cfi_restore esi;    .cfi_adjust_cfa_offset -4;"
            "pop edi;           .cfi_restore edi;    .cfi_adjust_cfa_offset -4;"
            "pop ebp;           .cfi_restore ebp;    .cfi_adjust_cfa_offset -4;"
            "ret;               .cfi_restore eip;    .cfi_adjust_cfa_offset -4;"
            :: "i" (switch_thread)
            : "cc", "memory"
        );
    }

    void scheduler::yield()
    {
        if (dpmi::in_irq_context()) [[unlikely]] return;

        dpmi::interrupt_unmask enable_interrupts { };
        auto* const ct = current_thread();

        debug::break_with_signal(debug::detail::thread_switched);

        {
            debug::trap_mask dont_trace_here { };
            context_switch(&ct->context);
        }

        dpmi::fpu_context::update_cr0();

#       ifndef NDEBUG
        if (ct->id != thread::main_thread_id and *reinterpret_cast<std::uint32_t*>(ct->stack.data()) != 0xDEADBEEF) [[unlikely]]
            throw std::runtime_error { "Stack overflow!" };
#       endif

        if (terminating) [[unlikely]]
            if (std::uncaught_exceptions() == 0)
                terminate();

        while (ct->invoke_list.size() > 0) [[unlikely]]
        {
            decltype(thread::invoke_list)::value_type f;
            {
                dpmi::interrupt_mask no_interrupts_please { };
                f = std::move(ct->invoke_list.front());
                ct->invoke_list.pop_front();
            }
            f();
        }

        if (ct->aborted) [[unlikely]]
            if (ct->state != thread::finishing and std::uncaught_exceptions() == 0)
                throw abort_thread { };
    }

    void scheduler::start_thread(const thread_ptr& t)
    {
        if (t->state != thread::starting) return;
        debug::trap_mask dont_trace_here { };
        dpmi::interrupt_mask no_interrupts_please { };
        threads->emplace_hint(threads->end(), t->id, t);
    }

    // The actual thread.
    void scheduler::run_thread() noexcept
    {
        auto* const t = current_thread();
        t->state = thread::running;
        try { (*t)(); }
        catch (const abort_thread& e) { e.defuse(); }
        catch (const terminate_exception& e) { terminating = true; e.defuse(); }
        catch (...)
        {
            fmt::print(stderr, FMT_STRING("caught exception from thread {:d}"), t->id);
#           ifndef NDEBUG
            fmt::print(stderr, FMT_STRING(" ({})"), t->name);
#           endif
            fmt::print(stderr, "\n");
            try { throw; }
            catch (std::exception& e) { print_exception(e); }
            terminating = true;
        }
        t->state = thread::finishing;
        atexit(t);
        t->state = thread::finished;

        debug::detail::notify_gdb_thread_event(debug::detail::thread_finished);

        while (true) yield();
    }

    // Select a new current_thread.
    thread_context* scheduler::switch_thread()
    {
        auto& it = iterator;
        thread* ct = (*it)->second.get();

        ct->eh_globals = get_eh_globals();

        for(std::size_t n = 0; ; ++n)
        {
            {
                dpmi::interrupt_mask no_interrupts_please { };
                if (ct->active()) [[likely]] ++*it;
                else *it = threads->erase(*it);
                if (*it == threads->end()) *it = threads->begin();
            }
            ct = (*it)->second.get();

            if (ct->state == thread::starting) [[unlikely]]     // new thread, initialize new context on stack
            {
#               ifndef NDEBUG
                *reinterpret_cast<std::uint32_t*>(ct->stack.data()) = 0xDEADBEEF;   // stack overflow protection
#               endif
                void* const esp = (ct->stack.data() + ct->stack.size_bytes() - 4) - sizeof(thread_context);
                ct->context = new (esp) thread_context { *main_thread->context };    // clone context from main thread
                ct->context->return_address = reinterpret_cast<std::uintptr_t>(run_thread);
            }

            if (not ct->suspended) [[likely]] break;
            if (n > threads->size())
            {
                debug::break_with_signal(debug::detail::all_threads_suspended);
                n = 0;
            }
        }
        set_eh_globals(ct->eh_globals);

        return ct->context;
    }

    void scheduler::atexit(thread* t)
    {
        for (const auto& f : t->atexit_list)
        {
            if (terminating) break;
            try { f(); }
            catch (const terminate_exception& e) { terminating = true; e.defuse(); }
            catch (...)
            {
                fmt::print(stderr, FMT_STRING("caught exception while processing atexit handlers on thread {:d}"), t->id);
#               ifndef NDEBUG
                fmt::print(stderr, FMT_STRING(" ({})"), t->name);
#               endif
                fmt::print(stderr, "\n");
                try { throw; }
                catch (std::exception& e) { print_exception(e); }
                terminating = true;
            }
        }
        t->atexit_list.clear();
    }
}
