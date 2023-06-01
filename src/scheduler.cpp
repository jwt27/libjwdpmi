/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2023 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#include <algorithm>
#include <memory_resource>
#include <cxxabi.h>
#include <unwind.h>
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
    static constinit bool terminating { false };
    static _Unwind_Ptr last_ip;

    static constexpr _Unwind_Exception_Class defused_class = 0;
    static constexpr _Unwind_Exception_Class active_class = 1;

    void scheduler::setup()
    {
        memres.emplace(64_KB);
        threads.emplace(memory_resource());

        thread& p = const_cast<thread&>(*threads->emplace().first);
        p.state = thread::running;
        p.set_name("Main thread");
        debug::throw_assert(p.id == thread::main_thread_id);

        iterator.emplace(threads->begin());

        int2f_handler.emplace(0x2f, [](dpmi::realmode_registers* reg, dpmi::far_ptr32)
        {
            if (reg->ax != 0x1680) return false;
            if (dpmi::in_irq_context()) return false;
            [[maybe_unused]] std::conditional_t<config::save_fpu_on_realmode_callback, empty, dpmi::fpu_context> fpu { };
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
        auto* main = get_thread(thread::main_thread_id);
        atexit(main);
        if (threads->size() == 1) [[likely]] return;
        fmt::print(stderr, FMT_STRING("Warning: exiting with active threads.\n"));
        std::vector<thread_id> ids;
        ids.reserve(threads->size());
        for (auto& ct : *threads)
        {
            if (ct.id == thread::main_thread_id) continue;
            ids.push_back(ct.id);
            auto& t = const_cast<thread&>(ct);
            t.cancel();
        }
        main->state = thread::running;
        for (auto id : ids)
        {
            thread* t;
            do
            {
                try { this_thread::yield(); }
                catch (const abi::__forced_unwind&) { catch_forced_unwind(); }
            } while ((t = get_thread(id)) and t->active());
        }
    }

    // Save the current task context, switch to a new task, and restore its context.
    void scheduler::context_switch(thread_context**)
    {
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

#       ifndef NDEBUG
        if (ct->id != thread::main_thread_id and *reinterpret_cast<std::uint32_t*>(ct->stack.data()) != 0xDEADBEEF) [[unlikely]]
            throw std::runtime_error { "Stack overflow!" };
#       endif

        if (terminating) [[unlikely]]
            if (std::uncaught_exceptions() == 0)
                forced_unwind();

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

        if (ct->canceled) [[unlikely]]
            if (ct->state != thread::finishing and std::uncaught_exceptions() == 0)
                forced_unwind();
    }

    // The actual thread.
    void scheduler::run_thread() noexcept
    {
        auto* const t = current_thread();
        t->state = thread::running;

        try
        {
            local_destructor finish { [t]
            {
                t->state = thread::finishing;
                atexit(t);
                t->state = thread::finished;

                debug::detail::notify_gdb_thread_event(debug::detail::thread_finished);
            } };

            (*t)();
        }
        catch (const abi::__forced_unwind& e) { }
        catch (...)
        {
            fmt::print(stderr, FMT_STRING("Caught exception from thread {:d}"), t->id);
#           ifndef NDEBUG
            fmt::print(stderr, FMT_STRING(" ({})"), t->name);
#           endif
            fmt::print(stderr, "\n");
            print_exception();
            terminating = true;
        }

        while (true) yield();
    }

    // Select a new current_thread.
    thread_context* scheduler::switch_thread()
    {
        dpmi::async_signal_mask disable_signals { };
        auto& it = iterator;
        thread* ct = current_thread();

        ct->eh_globals = *abi::__cxa_get_globals();
        ct->errno = errno;

        for(std::size_t n = 0; ; ++n)
        {
            {
                dpmi::interrupt_mask no_interrupts_please { };
                if (not ct->detached or ct->active()) [[likely]] ++*it;
                else *it = threads->erase(*it);
                if (*it == threads->end()) *it = threads->begin();
            }
            ct = current_thread();

            if (ct->state == thread::starting) [[unlikely]]     // new thread, initialize new context on stack
            {
#               ifndef NDEBUG
                *reinterpret_cast<std::uint32_t*>(ct->stack.data()) = 0xDEADBEEF;   // stack overflow protection
#               endif
                void* const esp = (ct->stack.data() + ct->stack.size_bytes() - 4) - sizeof(thread_context);
                ct->context = new (esp) thread_context { *threads->begin()->context };    // clone context from main thread
                ct->context->ebp = 0;
                ct->context->return_address = reinterpret_cast<std::uintptr_t>(run_thread);
            }

            if (not ct->suspended and ct->active()) [[likely]] break;
            if (n > threads->size())
            {
                debug::break_with_signal(debug::detail::all_threads_suspended);
                n = 0;
            }
        }
        *abi::__cxa_get_globals() = ct->eh_globals;
        errno = ct->errno;

        return ct->context;
    }

    void scheduler::atexit(thread* t) noexcept
    {
        for (const auto& f : t->atexit_list)
        {
            if (terminating) break;
            try { f(); }
            catch (const abi::__forced_unwind& e) { catch_forced_unwind(); }
            catch (...)
            {
                fmt::print(stderr, FMT_STRING("Caught exception while processing atexit handlers on thread {:d}"), t->id);
#               ifndef NDEBUG
                fmt::print(stderr, FMT_STRING(" ({})"), t->name);
#               endif
                fmt::print(stderr, "\n");
                print_exception();
                terminating = true;
            }
        }
        t->atexit_list.clear();
    }

    static void cleanup_forced_unwind(_Unwind_Reason_Code, _Unwind_Exception* exc) noexcept
    {
        if (exc->exception_class == defused_class) return;

        if (terminating)
        {
            fmt::print(stderr, FMT_STRING("Forced unwind got stuck at 0x{:x}.\n"), last_ip);
            std::terminate();
        }
        terminating = true;
    }

    static _Unwind_Reason_Code stop_forced_unwind(int, _Unwind_Action action, _Unwind_Exception_Class, _Unwind_Exception*, _Unwind_Context* context, void* param) noexcept
    {
        auto* const t = static_cast<detail::thread*>(param);

        last_ip = _Unwind_GetIP(context);

        if (not t->active())
            while (true) this_thread::yield();

        if (action & _UA_END_OF_STACK)
            std::terminate();

        return _URC_NO_REASON;
    }

    void scheduler::forced_unwind()
    {
        auto* const t = current_thread();
        t->unwind_exception.exception_class = active_class;
        t->unwind_exception.exception_cleanup = cleanup_forced_unwind;
        _Unwind_ForcedUnwind(&t->unwind_exception, stop_forced_unwind, t);
        __builtin_unreachable();
    }

    void scheduler::catch_forced_unwind() noexcept
    {
        current_thread()->unwind_exception.exception_class = defused_class;
    }
}

namespace jw
{
    void terminate()
    {
        detail::terminating = true;
        fmt::print(stderr, FMT_STRING("terminate() called at 0x{}.\n"), fmt::ptr(__builtin_return_address(0)));
        detail::scheduler::forced_unwind();
    }
}
