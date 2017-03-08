#include <iostream>
#include <algorithm>
#include <cassert>
#include <jw/thread/detail/scheduler.h>
#include <jw/thread/thread.h>

namespace jw
{
    namespace thread
    {
        namespace detail
        {
            scheduler::init_main scheduler::initializer;
            std::deque<thread_ptr> scheduler::threads { };
            thread_ptr scheduler::current_thread;
            thread_ptr scheduler::main_thread;

            scheduler::init_main::init_main()
            {
                main_thread = std::shared_ptr<thread>(new thread(0, 0));
                main_thread->state = running;
                main_thread->parent = main_thread;
                current_thread = main_thread;
            }

            // Save the current task context, switch to a new task, and restore its context.
            // May only be called from thread_switch()!
            void scheduler::context_switch() noexcept
            {
                asm volatile        // save the current context
                    ("sub esp, 4;"
                     "push ebp; push edi; push esi; push ebx;"
                     "push es; push fs; push gs;"
                     "mov eax, esp;"
                     : "=a" (current_thread->context)
                     :: "esp", "memory");

                set_next_thread();    // select a new current_thread

                asm volatile        // switch to the new context
                    ("mov esp, eax;"
                     "pop gs; pop fs; pop es;"
                     "pop ebx; pop esi; pop edi; pop ebp;"
                     "add esp, 4;"
                     "test dl, dl;"             // if starting a new thread
                     "jz context_switch_end;"
                     "and esp, -0x10;"          // align stack to 0x10 bytes
                     "mov ebp, esp;"
                     "jmp %2;"                  // jump to run_thread()
                     "context_switch_end:"
                     :: "a" (current_thread->context)
                     , "d" (current_thread->state == starting)
                     , "i" (run_thread)
                     : "esp", "esi", "edi", "ebx", "cc", "memory");
            }

            // Switches to the specified task, or the next task in queue if argument is nullptr.
            void scheduler::thread_switch(thread_ptr t)
            {
                if (t)
                {
                    threads.erase(remove_if(threads.begin(), threads.end(), [&](const auto& i) { return i == t; }), threads.end());
                    threads.push_front(t);
                }
                context_switch();   // switch to a new task context
                check_exception();  // rethrow pending exception
            }

            // The actual thread.
            // May only be jumped to from context_switch()!
            [[noreturn]]
            void scheduler::run_thread() noexcept
            {
                bool caught_exception = false;
                try
                {
                    current_thread->state = running;
                    current_thread->call();
                    current_thread->state = finished;
                }
                catch (const abort_thread&) { }
                catch (...) { catch_thread_exception(); caught_exception = true; }

                if (current_thread->state != finished) current_thread->state = initialized;

                while (true) try
                {
                    if (caught_exception) thread_switch(current_thread->parent);
                    else yield();
                }
                catch (const abort_thread&) { }
                catch (...) { catch_thread_exception(); caught_exception = true; }
            }

            // Checks if the exception is a thread_exception
            bool scheduler::is_thread_exception(const std::exception& exc) noexcept
            {
                try
                {
                    std::rethrow_if_nested(exc);
                    throw exc;
                }
                catch (const thread_exception& e) { return true; }
                catch (const std::exception& e) { return is_thread_exception(e); }
                catch (...) { }
                return false;
            }

            // Rethrows exceptions that occured on child threads.
            // Throws abort_thread if task->abort() is called, or orphaned_thread if task is orphaned.
            void scheduler::check_exception()
            {
                if (current_thread->awaiting && !current_thread->awaiting->exceptions.empty())
                {
                    auto exc = current_thread->awaiting->exceptions.front();
                    current_thread->awaiting->exceptions.pop_front();
                    std::rethrow_exception(exc);
                }
                if (!current_thread->exceptions.empty())
                {
                    for (auto exc : current_thread->exceptions)
                    {
                        try
                        {
                            std::rethrow_exception(exc);
                        }
                        catch (const std::exception& e)
                        {
                            if (is_thread_exception(e))
                            {
                                auto& exceptions = current_thread->exceptions;
                                exceptions.erase(remove_if(exceptions.begin(), exceptions.end(), [&](const auto& i) { return i == exc; }), exceptions.end());
                                throw;
                            }
                        }
                    }
                }
                if (current_thread->state == terminating) throw abort_thread();
                if (current_thread.unique() && !current_thread->allow_orphan) throw orphaned_thread();
            }

            void scheduler::catch_thread_exception() noexcept
            {
                try             // wrap the current exception in a nested exception
                { std::throw_with_nested(thread_exception(current_thread)); }
                catch (...)
                { current_thread->exceptions.push_back(std::current_exception()); }
            }

            // Selects a new current_thread.
            // May only be called from context_switch()!
            void scheduler::set_next_thread() noexcept        // TODO: catch exceptions here (from deque, shared_ptr) and do something sensible
            {
                do
                {
                    if (current_thread->is_running()) threads.push_back(current_thread);

                    current_thread = threads.front();
                    threads.pop_front();

                    if (current_thread->state == starting) // new task, initialize new context on stack
                    {
                        byte* esp = (current_thread->stack_ptr + current_thread->stack_size - 4) - sizeof(thread_context);

                        current_thread->context = reinterpret_cast<thread_context*>(esp);       // *context points to top of stack
                        if (current_thread->parent == nullptr) current_thread->parent = main_thread;
                        *current_thread->context = *current_thread->parent->context;            // clone parent's context to new stack
                    }

                    assert(current_thread != nullptr);
                    assert(reinterpret_cast<byte*>(current_thread->context) > current_thread->stack_ptr);   // stack overflow, probably pointless to check this here.
                                                                                                            // maybe set a magic value at the end and see if it's overwritten
                    if (current_thread->exceptions.size() != 0) break;
                    if (current_thread->awaiting && current_thread->awaiting->exceptions.size() != 0) break;
                } while (current_thread->state == suspended);
            }
        }
    }
}