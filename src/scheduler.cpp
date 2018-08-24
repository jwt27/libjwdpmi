/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#include <algorithm>
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
                new(p) thread { 1 };
                main_thread = std::shared_ptr<thread> { p };
                main_thread->state = running;
                main_thread->parent = main_thread;
                main_thread->name = "Main thread";
                current_thread = main_thread;
            }

            // Save the current task context, switch to a new task, and restore its context.
            void scheduler::context_switch()
            {
                asm volatile            // save the current context
                (
                    ".cfi_def_cfa esp, 4;"
                    "push ebp; .cfi_adjust_cfa_offset 4; .cfi_rel_offset ebp, 0;"
                    "push edi; .cfi_adjust_cfa_offset 4; .cfi_rel_offset edi, 0;"
                    "push esi; .cfi_adjust_cfa_offset 4; .cfi_rel_offset esi, 0;"
                    "push ebx; .cfi_adjust_cfa_offset 4; .cfi_rel_offset ebx, 0;"
                    "push es; .cfi_adjust_cfa_offset 4; .cfi_rel_offset es, 0;"
                    "push fs; .cfi_adjust_cfa_offset 4; .cfi_rel_offset fs, 0;"
                    "push gs; .cfi_adjust_cfa_offset 4; .cfi_rel_offset gs, 0;"
                    "mov eax, esp;"
                    : "=a" (current_thread->context)
                    :: "esp", "cc", "memory"
                );

                set_next_thread();      // select a new current_thread

                asm volatile            // switch to the new context
                (
                    "mov esp, eax;"
                    "pop gs; .cfi_restore gs; .cfi_adjust_cfa_offset -4;"
                    "pop fs; .cfi_restore fs; .cfi_adjust_cfa_offset -4;"
                    "pop es; .cfi_restore es; .cfi_adjust_cfa_offset -4;"
                    "pop ebx; .cfi_restore ebx; .cfi_adjust_cfa_offset -4;"
                    "pop esi; .cfi_restore esi; .cfi_adjust_cfa_offset -4;"
                    "pop edi; .cfi_restore edi; .cfi_adjust_cfa_offset -4;"
                    "pop ebp; .cfi_restore ebp; .cfi_adjust_cfa_offset -4;"
                    "ret;"
                    :: "a" (current_thread->context)
                    : "esp", "cc", "memory"
                );
            }

            // Switches to the specified task, or the next task in queue if argument is nullptr.
            void scheduler::thread_switch(thread_ptr t)
            {
                debug::trap_mask dont_trace_here { };
                if (__builtin_expect(t != nullptr, false))
                {
                    dpmi::interrupt_mask no_interrupts_please { };
                    threads.erase(remove_if(threads.begin(), threads.end(), [t](const auto& i) { return i == t; }), threads.end());
                    threads.push_front(t);
                }

                if (dpmi::in_irq_context() or std::uncaught_exceptions() > 0) return;

                debug::break_with_signal(debug::detail::thread_switched);
                context_switch();   // switch to a new task context
                check_exception();  // rethrow pending exception

                while (__builtin_expect(current_thread->invoke_list.size() > 0, false))
                {
                    current_thread->invoke_list.front()();
                    current_thread->invoke_list.pop_front();
                }
            }

            // The actual thread.
            void scheduler::run_thread() noexcept
            {
                try
                {
                    current_thread->state = running;
                    current_thread->call();
                    current_thread->state = finished;
                }
                catch (const abort_thread&) { }
                catch (const terminate_exception&) 
                { 
                    for (auto& t : threads) t->exceptions.push_back(std::current_exception());
                }
                catch (...) 
                { 
                    current_thread->exceptions.push_back(std::current_exception()); 
                }

                if (current_thread->state != finished) current_thread->state = initialized;
                debug::detail::notify_gdb_thread_event(debug::detail::thread_finished);

                while (true) try { yield(); }
                catch (const abort_thread&) { }
                catch (...) 
                { 
                    current_thread->exceptions.push_back(std::current_exception()); 
                }
            }

            // Rethrows exceptions that occured on child threads.
            // Throws abort_thread if task->abort() is called, or orphaned_thread if task is orphaned.
            void scheduler::check_exception()
            {
                if (__builtin_expect(current_thread->awaiting && current_thread->awaiting->pending_exceptions() > 0, false))
                {
                    auto exc = current_thread->awaiting->exceptions.front();
                    current_thread->awaiting->exceptions.pop_front();
                    try { std::rethrow_exception(exc); }
                    catch (...) { std::throw_with_nested(thread_exception { current_thread }); }
                }

                if (__builtin_expect(current_thread->pending_exceptions() > 0, false))
                {
                    auto& exceptions = current_thread->exceptions;
                    for (auto i = exceptions.begin(); i != exceptions.end(); ++i)
                    {
                        try { std::rethrow_exception(*i); }
                        catch (const thread_exception& e)
                        {
                            if (e.thread.use_count() > 1) continue; // Only rethrow if exception came from deleted/orphaned threads.
                            exceptions.erase(i);
                            throw;
                        }
                        catch (terminate_exception& e)
                        {
                            auto& exceptions = current_thread->exceptions;
                            exceptions.erase(i);
                            throw;
                        }
                        catch (...) { }
                    }
                }
                
                if (__builtin_expect(current_thread != main_thread && *reinterpret_cast<std::uint32_t*>(current_thread->stack.get()) != 0xDEADBEEF, false))
                    throw std::runtime_error("Stack overflow!");

                if (__builtin_expect(current_thread->state == terminating, false)) throw abort_thread();
                if (__builtin_expect(current_thread.unique() && !current_thread->allow_orphan && current_thread->is_running(), false)) throw orphaned_thread();
            }

            // Selects a new current_thread.
            // May only be called from context_switch()!
            void scheduler::set_next_thread()
            {
                dpmi::interrupt_mask no_interrupts_please { };
                for(std::size_t i = 0; ; ++i)
                {
                    if (__builtin_expect(current_thread->is_running(), true)) threads.push_back(current_thread);

                    current_thread = threads.front();
                    threads.pop_front();

                    if (__builtin_expect(current_thread->state == starting, false)) // new task, initialize new context on stack
                    {
                        byte* esp = (current_thread->stack.get() + current_thread->stack_size - 4) - sizeof(thread_context);
                        *reinterpret_cast<std::uint32_t*>(current_thread->stack.get()) = 0xDEADBEEF;  // stack overflow protection

                        current_thread->context = reinterpret_cast<thread_context*>(esp);           // *context points to top of stack
                        if (current_thread->parent == nullptr) current_thread->parent = main_thread;
                        *current_thread->context = *current_thread->parent->context;                // clone parent's context to new stack
                        current_thread->context->return_address = reinterpret_cast<std::uintptr_t>(run_thread);
                    }

                    if (__builtin_expect(current_thread->pending_exceptions() != 0, false)) break;
                    if (__builtin_expect(current_thread->awaiting && current_thread->awaiting->pending_exceptions() != 0, false)) break;
                    if (__builtin_expect(current_thread->state != suspended, true)) return;
                    if (i > threads.size())
                    {
                        debug::break_with_signal(debug::detail::all_threads_suspended);
                        i = 0;
                    }
                }
            }
        }
    }
}
