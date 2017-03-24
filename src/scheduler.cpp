/******************************* libjwdpmi **********************************
    Copyright (C) 2016-2017  J.W. Jagersma

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
                                                                            */

#include <algorithm>
#include <jw/dpmi/irq_mask.h>
#include <jw/thread/detail/scheduler.h>
#include <jw/thread/thread.h>

namespace jw
{
    namespace thread
    {
        namespace detail
        {
            std::uint32_t thread::id_count { 0 };

            scheduler::init_main scheduler::initializer;
            dpmi::locked_pool_allocator<> scheduler::alloc { 128_KB };
            std::deque<thread_ptr, dpmi::locked_pool_allocator<>> scheduler::threads { alloc };
            thread_ptr scheduler::current_thread;
            thread_ptr scheduler::main_thread;

            scheduler::init_main::init_main()
            {
                main_thread = std::shared_ptr<thread> { new thread(0, nullptr) };
                main_thread->state = running;
                main_thread->parent = main_thread;
                main_thread->name = "Main thread";
                current_thread = main_thread;
            }

            // Save the current task context, switch to a new task, and restore its context.
            // May only be called from thread_switch()!
            void scheduler::context_switch() noexcept
            {
                asm volatile            // save the current context
                    ("sub esp, 4;"
                     "push ebp; push edi; push esi; push ebx;"
                     "push es; push fs; push gs;"
                     "mov eax, esp;"
                     : "=a" (current_thread->context)
                     :: "esp", "cc", "memory");

                set_next_thread();      // select a new current_thread

                asm volatile            // switch to the new context
                    ("mov esp, eax;"
                     "pop gs; pop fs; pop es;"
                     "pop ebx; pop esi; pop edi; pop ebp;"
                     "add esp, 4;"
                     "test dl, dl;"             // if starting a new thread
                     "jz context_switch_end;"
                     "and esp, -0x10;"          // align stack to 0x10 bytes
                     "mov ebp, esp;"
                     "mov [esp], esp;"
                     "jmp %2;"                  // jump to run_thread()
                     "context_switch_end:"
                     :: "a" (current_thread->context)
                     , "d" (current_thread->state == starting)
                     , "i" (run_thread)
                     : "esp", "cc", "memory");
            }

            // Switches to the specified task, or the next task in queue if argument is nullptr.
            void scheduler::thread_switch(thread_ptr t)
            {
                dpmi::trap_mask dont_trace_here { };
                if (t)
                {
                    dpmi::interrupt_mask no_interrupts_please { };
                    threads.erase(remove_if(threads.begin(), threads.end(), [&](const auto& i) { return i == t; }), threads.end());
                    threads.push_front(t);
                }
                if (dpmi::in_irq_context()) return;
                context_switch();   // switch to a new task context
                check_exception();  // rethrow pending exception
            }

            // The actual thread.
            // May only be jumped to from context_switch()!
            [[noreturn]]
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
                if (current_thread->awaiting && current_thread->awaiting->pending_exceptions() > 0)
                {
                    auto exc = current_thread->awaiting->exceptions.front();
                    current_thread->awaiting->exceptions.pop_front();
                    try { std::rethrow_exception(exc); }
                    catch (...) { std::throw_with_nested(thread_exception { current_thread }); }
                }
                for (auto exc : current_thread->exceptions)
                {
                    try { std::rethrow_exception(exc); }
                    catch (const thread_exception& e)
                    {
                        if (e.task_ptr.use_count() > 1) continue; // Only rethrow if exception came from deleted/orphaned threads.
                        auto& exceptions = current_thread->exceptions;
                        exceptions.erase(remove_if(exceptions.begin(), exceptions.end(), [&](const auto& i) { return i == exc; }), exceptions.end());
                        throw;
                    }
                    catch (terminate_exception& e)
                    {
                        auto& exceptions = current_thread->exceptions;
                        exceptions.erase(remove_if(exceptions.begin(), exceptions.end(), [&](const auto& i) { return i == exc; }), exceptions.end());
                        throw;
                    }
                    catch (...) { }
                }
                
                if (current_thread != main_thread && *reinterpret_cast<std::uint32_t*>(current_thread->stack_ptr) != 0xDEADBEEF)
                    throw std::runtime_error("Stack overflow!");

                if (current_thread->state == terminating) throw abort_thread();
                if (current_thread.unique() && !current_thread->allow_orphan && current_thread->is_running()) throw orphaned_thread();
            }

            // Selects a new current_thread.
            // May only be called from context_switch()!
            void scheduler::set_next_thread() noexcept        // TODO: catch exceptions here (from deque, shared_ptr) and do something sensible
            {
                dpmi::interrupt_mask no_interrupts_please { };
                do
                {
                    if (current_thread->is_running()) threads.push_back(current_thread);

                    current_thread = threads.front();
                    threads.pop_front();

                    if (current_thread->state == starting) // new task, initialize new context on stack
                    {
                        byte* esp = (current_thread->stack_ptr + current_thread->stack_size - 4) - sizeof(thread_context);
                        *reinterpret_cast<std::uint32_t*>(current_thread->stack_ptr) = 0xDEADBEEF;  // stack overflow protection

                        current_thread->context = reinterpret_cast<thread_context*>(esp);           // *context points to top of stack
                        if (current_thread->parent == nullptr) current_thread->parent = main_thread;
                        *current_thread->context = *current_thread->parent->context;                // clone parent's context to new stack
                    }

                    if (current_thread->pending_exceptions() != 0) break;
                    if (current_thread->awaiting && current_thread->awaiting->pending_exceptions() != 0) break;
                } while (current_thread->state == suspended);
            }
        }
    }
}
