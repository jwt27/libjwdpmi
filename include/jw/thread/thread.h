/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <stdexcept>
#include <sstream>
#include <jw/thread/detail/scheduler.h>

namespace jw
{
    namespace thread
    {
        // Thrown when task->abort() is called.
        struct abort_thread : public std::exception
        {
            virtual const char* what() const noexcept override { return "Task aborted."; }
        };

        // Thrown when a task is still running, but no longer referenced anywhere.
        // By default, threads may not be orphaned. This behaviour can be changed with task->allow_orphan.
        struct orphaned_thread : public abort_thread
        {
            virtual const char* what() const noexcept override { return "Task orphaned, aborting."; }
        };

        // Thrown when task->await() is called while no result will ever be available.
        struct illegal_await : public std::exception
        {
            virtual const char* what() const noexcept override 
            { 
                std::stringstream s;
                s << "Illegal call to await().\n"; 
                s << "Thread" << thread->name << " (id " << std::dec << thread->id() << ")";
                return s.str().c_str();
            }
            const detail::thread_ptr thread;
            illegal_await(const detail::thread_ptr& t) noexcept : thread(t) { }
        };

        // Thrown on parent thread in a nested_exception when an unhandled exception occurs on a child thread.
        struct thread_exception : public std::exception
        {
            virtual const char* what() const noexcept override 
            {
                std::stringstream s;
                s << "Exception thrown from thread";
                if (auto t = thread.lock()) s << ": " << t->name << " (id " << std::dec << t->id() << ")";
                else s << '.';
                return s.str().c_str();
            }
            const std::weak_ptr<detail::thread> thread;
            thread_exception(const detail::thread_ptr& t) noexcept : thread(t) { }
        };

        // Yields execution to the next thread in the queue.
        inline void yield() 
        { 
            if (dpmi::in_irq_context()) return;
            dpmi::trap_mask dont_trace_here { };
            detail::scheduler::thread_switch(); 
        }

        // Yields execution while the given condition evaluates to true.
        template<typename F> inline void yield_while(F condition) 
        { 
            if (dpmi::in_irq_context()) return;
            dpmi::trap_mask dont_trace_here { };
            while (condition()) yield();
        };

        // Yields execution until the given time point.
        template<typename T> inline void yield_until(T time_point)
        { 
            if (dpmi::in_irq_context()) return;
            dpmi::trap_mask dont_trace_here { };
            yield_while([&time_point] { return T::clock::now() < time_point; });
        };

        // Yields execution for the given duration.
        template<typename C> inline void yield_for(typename C::duration duration)
        { 
            if (dpmi::in_irq_context()) return;
            dpmi::trap_mask dont_trace_here { };
            yield_until(C::now() + duration);
        };

        // Combination of yield_while() and yield_until(). Returns condition(), which is true on timeout.
        template<typename T, typename F> inline bool yield_while_until(F condition, T time_point)
        { 
            if (dpmi::in_irq_context()) return condition();
            dpmi::trap_mask dont_trace_here { };
            bool c;
            yield_while([&time_point, &condition, &c] { return T::clock::now() < time_point && (c = condition()); });
            return c;
        };

        // Combination of yield_while() and yield_for(). Returns condition(), which is true on timeout.
        template<typename C, typename F> inline bool yield_while_for(F condition, typename C::duration duration) 
        { 
            if (dpmi::in_irq_context()) return condition();
            dpmi::trap_mask dont_trace_here { };
            return yield_while_until(condition, C::now() + duration);
        };
    }
}
