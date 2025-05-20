/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2016 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#include <jw/main.h>
#include <jw/dpmi/irq_mask.h>
#include <jw/detail/scheduler.h>
#include <jw/thread.h>
#include <jw/dpmi/realmode.h>
#include <fmt/format.h>
#include <cxxabi.h>
#include <unwind.h>
#include <algorithm>
#include <memory_resource>
#ifdef JWDPMI_WITH_WATT32
# include <tcp.h>
#endif

extern "C" void __wrap___dpmi_yield()
{
    jw::detail::scheduler::safe_yield();
    errno = 0;
}

namespace jw::detail
{
    static constinit bool terminating { false };
    static _Unwind_Ptr last_ip;

    void scheduler::setup()
    {
        memres.emplace(64_KB);
        threads.emplace(memory_resource());

        thread& p = const_cast<thread&>(*threads->emplace().first);
        p.state = thread::running;
        p.set_name("Main thread");
        debug::throw_assert(p.id == thread::main_thread_id);

        iterator.emplace(threads->begin());

#       ifdef JWDPMI_WITH_WATT32
        sock_yield(nullptr, safe_yield);
#       endif
    }

    void scheduler::kill_all()
    {
        auto* main = get_thread(thread::main_thread_id);
        atexit(main);
        if (threads->size() == 1) [[likely]] return;
        fmt::print(stderr, "Warning: exiting with active threads.\n");
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
        asm
        (
            "                   .cfi_def_cfa esp, 4; .cfi_rel_offset eip, 0;"
            "push ebp;          .cfi_adjust_cfa_offset 4; .cfi_rel_offset ebp, 0;"
            "push edi;          .cfi_adjust_cfa_offset 4; .cfi_rel_offset edi, 0;"
            "push esi;          .cfi_adjust_cfa_offset 4; .cfi_rel_offset esi, 0;"
            "push ebx;          .cfi_adjust_cfa_offset 4; .cfi_rel_offset ebx, 0;"
            "sub esp, 4;        .cfi_adjust_cfa_offset 4;"
            "push fs;           .cfi_adjust_cfa_offset 4; .cfi_rel_offset fs, 0;"
            "push gs;           .cfi_adjust_cfa_offset 4; .cfi_rel_offset gs, 0;"
            "mov [eax], esp;"
            "call %0;"
            "mov esp, eax;"
            "pop gs;            .cfi_restore gs;     .cfi_adjust_cfa_offset -4;"
            "pop fs;            .cfi_restore fs;     .cfi_adjust_cfa_offset -4;"
            "add esp, 4;                             .cfi_adjust_cfa_offset -4;"
            "pop ebx;           .cfi_restore ebx;    .cfi_adjust_cfa_offset -4;"
            "pop esi;           .cfi_restore esi;    .cfi_adjust_cfa_offset -4;"
            "pop edi;           .cfi_restore edi;    .cfi_adjust_cfa_offset -4;"
            "pop ebp;           .cfi_restore ebp;    .cfi_adjust_cfa_offset -4;"
            "ret;               .cfi_restore eip;    .cfi_adjust_cfa_offset -4;"
            :: "i" (switch_thread)
            : "memory"
        );
    }

    template<bool allow_unwind>
    inline void scheduler::do_yield()
    {
        if (dpmi::in_irq_context()) [[unlikely]] return;

        dpmi::interrupt_unmask enable_interrupts { };
        auto* const ct = current_thread();

        {
            debug::trap_mask dont_trace_here { };
            context_switch(&ct->context);
        }

#       ifndef NDEBUG
        if (ct->id != thread::main_thread_id and *reinterpret_cast<std::uint32_t*>(ct->stack.data()) != 0xDEADBEEF) [[unlikely]]
            throw std::runtime_error { "Stack overflow!" };
#       endif

        if constexpr (allow_unwind)
            if (terminating) [[unlikely]]
                if (not ct->unwinding and std::uncaught_exceptions() == 0)
                    forced_unwind();

        while (not ct->invoke_list.empty()) [[unlikely]]
        {
            finally pop { [ct]
            {
                asm ("cli");
                ct->invoke_list.pop_front();
                asm ("sti");
            } };
            ct->invoke_list.front()();
        }

        if constexpr (allow_unwind)
            if (ct->canceled) [[unlikely]]
                if (ct->state != thread::finishing and not ct->unwinding and std::uncaught_exceptions() == 0)
                    forced_unwind();
    }

    void scheduler::yield()      { do_yield<true>(); }
    void scheduler::safe_yield() { do_yield<false>(); }

    // The actual thread.
    void scheduler::run_thread() noexcept
    {
        auto* const t = current_thread();
        debug::detail::create_thread(t);
        t->state = thread::running;

        try
        {
            finally finish { [t]
            {
                t->state = thread::finishing;
                atexit(t);
                t->state = thread::finished;
            } };

            (*t)();
        }
        catch (const abi::__forced_unwind& e) { catch_forced_unwind(); }
        catch (...)
        {
            fmt::print(stderr, "Caught exception from thread {:d}", t->id);
#           ifndef NDEBUG
            fmt::print(stderr, " ({})", t->name);
#           endif
            fmt::print(stderr, "\n");
            print_exception();
            terminating = true;
        }

        debug::detail::destroy_thread(t);
        while (true) yield();
    }

    // Select a new current_thread.
    thread_context* scheduler::switch_thread()
    {
        dpmi::async_signal_mask disable_signals { };
        thread* ct = current_thread();

        ct->eh_globals = *abi::__cxa_get_globals();
        ct->errno = errno;

        do
        {
            {
                finally sti { [] { asm ("sti"); } };
                auto it = *iterator;
                if (ct->active() | not ct->detached) [[likely]]
                    ++it;
                else
                {
                    // Interrupts are always enabled here (by yield()).
                    asm ("cli");
                    it = threads->erase(it);
                }
                if (it == threads->end())
                    it = threads->begin();
                std::atomic_ref { *iterator }.store(it, std::memory_order_release);
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
        } while (ct->suspended | not ct->active());

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
                fmt::print(stderr, "Caught exception while processing atexit handlers on thread {:d}", t->id);
#               ifndef NDEBUG
                fmt::print(stderr, " ({})", t->name);
#               endif
                fmt::print(stderr, "\n");
                print_exception();
                terminating = true;
            }
        }
        t->atexit_list.clear();
    }

    static void cleanup_forced_unwind(_Unwind_Reason_Code, _Unwind_Exception*) noexcept
    {
        if (not scheduler::current_thread()->is_unwinding()) return;

        if (terminating)
        {
            fmt::print(stderr, "Forced unwind got stuck at 0x{:x}.\n", last_ip);
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
        t->unwinding = true;
        t->unwind_exception.exception_class = 0;
        t->unwind_exception.exception_cleanup = cleanup_forced_unwind;
        _Unwind_ForcedUnwind(&t->unwind_exception, stop_forced_unwind, t);
        __builtin_unreachable();
    }

    void scheduler::catch_forced_unwind() noexcept
    {
        auto* const t = current_thread();
        t->unwinding = false;
    }
}

namespace jw
{
    void terminate()
    {
        detail::terminating = true;
        fmt::print(stderr, "terminate() called.\n");
        debug::stacktrace<64>::current(1).print();
        detail::scheduler::forced_unwind();
    }
}
