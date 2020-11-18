/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <stdexcept>
#include <sstream>
#include <jw/thread/detail/scheduler.h>
#include <../jwdpmi_config.h>

namespace jw
{
    namespace thread
    {
        // Thrown when task->abort() is called.
        struct abort_thread
        {
            virtual const char* what() const noexcept { return "Task aborted"; }
        };

        // Thrown when a task is still running, but no longer referenced anywhere.
        // By default, threads may not be orphaned. This behaviour can be changed with task->allow_orphan.
        struct orphaned_thread : public abort_thread
        {
            virtual const char* what() const noexcept override { return "Task orphaned, aborting"; }
        };

        // Thrown when task->await() is called while no result will ever be available.
        struct illegal_await : public std::exception
        {
            virtual const char* what() const noexcept override { return "Illegal call to await()"; }
            illegal_await(const detail::thread_ptr& t) noexcept : thread { t } { }

            const detail::thread_ptr thread;
        };

        // Thrown on parent thread in a nested_exception when an unhandled exception occurs on a child thread.
        struct thread_exception : public std::exception
        {
            virtual const char* what() const noexcept override { return "Exception thrown from thread"; }
            thread_exception(const detail::thread_ptr& t) noexcept : thread { t } { }

            const std::weak_ptr<detail::thread> thread;
        };

        // Yields execution to the next thread in the queue.
        inline void yield()
        {
            if (dpmi::in_irq_context()) return;
            debug::trap_mask dont_trace_here { };
            detail::scheduler::thread_switch();
        }

        // Yields execution while the given condition evaluates to true.
        template<typename F> inline void yield_while(F&& condition)
        {
            while (condition()) yield();
        };

        // Yields execution until the given time point.
        template<typename P> inline void yield_until(const P& time_point)
        {
            yield_while([&time_point] { return P::clock::now() < time_point; });
        };

        // Yields execution for the given duration.
        template<typename C = config::thread_clock> inline void yield_for(const typename C::duration& duration)
        {
            yield_until(C::now() + duration);
        };

        // Combination of yield_while() and yield_until(). Returns true on timeout.
        template<typename F, typename P> inline bool yield_while_until(F&& condition, const P& time_point)
        {
            bool c;
            yield_while([&] { return (c = condition()) and P::clock::now() < time_point; });
            return c;
        };

        // Combination of yield_while() and yield_for(). Returns true on timeout.
        template<typename C = config::thread_clock, typename F> inline bool yield_while_for(F&& condition, const typename C::duration& duration)
        {
            return yield_while_until(condition, C::now() + duration);
        };

        // Call a function on the main thread.
        template<typename F> void invoke_main(F&& function) { detail::scheduler::invoke_main(std::forward<F>(function)); }
    }
}
