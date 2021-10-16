/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#include <algorithm>
#include <jw/main.h>
#include <jw/dpmi/irq_mask.h>
#include <jw/thread/detail/scheduler.h>
#include <jw/thread/thread.h>
#include <jw/debug/debug.h>
#include <jw/debug/detail/signals.h>

namespace jw
{
    namespace debug::detail
    {
        void notify_gdb_thread_event(debug::detail::debug_signals);
    }

    namespace thread
    {
        namespace detail
        {
            scheduler::init_main::init_main()
            {
                pool_alloc = &alloc;
                using rebind = typename std::allocator_traits<decltype(alloc)>::rebind_alloc<thread>;
                auto* p = rebind { alloc }.allocate(1);
                new(p) thread { nullptr, 0 };
                main_thread = std::shared_ptr<thread> { p };
                main_thread->state = running;
                main_thread->name = "Main thread";
                current_thread = main_thread;
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
                    "push es;           .cfi_adjust_cfa_offset 4; .cfi_rel_offset es, 0;"
                    "push fs;           .cfi_adjust_cfa_offset 4; .cfi_rel_offset fs, 0;"
                    "push gs;           .cfi_adjust_cfa_offset 4; .cfi_rel_offset gs, 0;"
                    "mov eax, [esp+0x20];"
                    "mov [eax], esp;"
                    "call %0;"
                    "mov esp, eax;"
                    "pop gs;            .cfi_restore gs; .cfi_adjust_cfa_offset -4;"
                    "pop fs;            .cfi_restore fs; .cfi_adjust_cfa_offset -4;"
                    "pop es;            .cfi_restore es; .cfi_adjust_cfa_offset -4;"
                    "pop ebx;           .cfi_restore ebx; .cfi_adjust_cfa_offset -4;"
                    "pop esi;           .cfi_restore esi; .cfi_adjust_cfa_offset -4;"
                    "pop edi;           .cfi_restore edi; .cfi_adjust_cfa_offset -4;"
                    "pop ebp;           .cfi_restore ebp; .cfi_adjust_cfa_offset -4;"
                    "ret;               .cfi_restore eip; .cfi_adjust_cfa_offset -4;"
                    :: "i" (set_next_thread)
                    : "cc", "memory"
                );
            }

            void scheduler::thread_switch()
            {
                debug::trap_mask dont_trace_here{ };

                if (dpmi::in_irq_context() or std::uncaught_exceptions() > 0) [[unlikely]] return;

                debug::break_with_signal(debug::detail::thread_switched);
                context_switch(&current_thread->context);   // switch to a new task context
                check_exception();  // rethrow pending exception

                while (current_thread->invoke_list.size() > 0) [[unlikely]]
                {
                    decltype(thread::invoke_list)::value_type f;
                    {
                        dpmi::interrupt_mask no_interrupts_please { };
                        f = std::move(current_thread->invoke_list.front());
                        current_thread->invoke_list.pop_front();
                    }
                    f();
                }
            }

            void scheduler::start_thread(const thread_ptr& t)
            {
                debug::trap_mask dont_trace_here { };
                dpmi::interrupt_mask no_interrupts_please { };

                threads.erase(remove_if(threads.begin(), threads.end(), [t](const auto& i) { return i == t; }), threads.end());
                threads.push_front(t);
                thread_switch();
            }

            // The actual thread.
            void scheduler::run_thread() noexcept
            {
                auto* const t = current_thread.get();
                try
                {
                    t->state = running;
                    t->function();
                    t->state = finished;
                }
                catch (const abort_thread& e) { e.defuse(); }
                catch (const terminate_exception&) { terminating = true; }
                catch (...)
                {
                    std::cerr << "caught exception from thread " << t->id;
                    std::cerr << " (" << t->name << ")\n";
                    try { throw; }
                    catch (std::exception& e) { print_exception(e); }
                    terminating = true;
                }

                if (t->state != finished) t->state = aborted;
                debug::detail::notify_gdb_thread_event(debug::detail::thread_finished);

                while (true) yield();
            }

            void scheduler::check_exception()
            {
#           ifndef NDEBUG
                if (current_thread != main_thread and *reinterpret_cast<std::uint32_t*>(current_thread->stack.data()) != 0xDEADBEEF) [[unlikely]]
                    throw std::runtime_error("Stack overflow!");
#           endif

                if (terminating) [[unlikely]] terminate();
                if (current_thread->state == aborting) [[unlikely]] throw abort_thread();
            }

            // Select a new current_thread.
            thread_context* scheduler::set_next_thread()
            {
                for(std::size_t i = 0; ; ++i)
                {
                    {
                        dpmi::interrupt_mask no_interrupts_please { };
                        if (current_thread->active()) [[likely]] threads.emplace_back(std::move(current_thread));
                        current_thread = std::move(threads.front());
                        threads.pop_front();
                    }

                    if (current_thread->state == starting) [[unlikely]]     // new thread, initialize new context on stack
                    {
#                   ifndef NDEBUG
                        *reinterpret_cast<std::uint32_t*>(current_thread->stack.data()) = 0xDEADBEEF;   // stack overflow protection
#                   endif
                        byte* const esp = (current_thread->stack.data() + current_thread->stack.size_bytes() - 4) - sizeof(thread_context);
                        current_thread->context = reinterpret_cast<thread_context*>(esp);               // *context points to top of stack
                        *current_thread->context = *threads.back()->context;
                        current_thread->context->return_address = reinterpret_cast<std::uintptr_t>(run_thread);
                    }

                    if (current_thread->state != suspended) [[likely]] break;
                    if (i > threads.size())
                    {
                        debug::break_with_signal(debug::detail::all_threads_suspended);
                        i = 0;
                    }
                }
                return current_thread->context;
            }
        }
    }
}
