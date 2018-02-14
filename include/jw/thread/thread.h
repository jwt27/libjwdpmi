/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <stdexcept>
#include <sstream>
#include <jw/thread/detail/scheduler.h>

namespace jw
{
    namespace chrono
    {
        struct pit;
    }

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
        inline bool yield_while(auto&& condition)
        {
            if (dpmi::in_irq_context()) return condition();
            dpmi::trap_mask dont_trace_here { };
            while (condition()) yield();
            return false;
        };

        // Yields execution until the given time point.
        template<typename P> inline void yield_until(const P& time_point)
        {
            if (dpmi::in_irq_context()) return;
            dpmi::trap_mask dont_trace_here { };
            yield_while([&time_point] { return P::clock::now() < time_point; });
        };

        // Yields execution for the given duration.
        template<typename C = chrono::pit> inline void yield_for(const typename C::duration& duration)
        {
            if (dpmi::in_irq_context()) return;
            dpmi::trap_mask dont_trace_here { };
            yield_until(C::now() + duration);
        };

        // Combination of yield_while() and yield_until(). Returns true on timeout.
        template<typename P> inline bool yield_while_until(auto&& condition, const P& time_point)
        {
            if (dpmi::in_irq_context()) return condition();
            dpmi::trap_mask dont_trace_here { };
            bool c;
            yield_while([&] { return (c = condition()) && P::clock::now() < time_point; });
            return c;
        };

        // Combination of yield_while() and yield_for(). Returns true on timeout.
        template<typename C = chrono::pit> inline bool yield_while_for(auto&& condition, const typename C::duration& duration)
        {
            if (dpmi::in_irq_context()) return condition();
            dpmi::trap_mask dont_trace_here { };
            return yield_while_until(condition, C::now() + duration);
        };
    }
}
