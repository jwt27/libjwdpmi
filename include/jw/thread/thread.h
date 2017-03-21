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

#pragma once
#include <stdexcept>
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
            virtual const char* what() const noexcept override { return "Illegal call to await()."; }
            const detail::thread_ptr task_ptr;
            illegal_await(const detail::thread_ptr& t) noexcept : task_ptr(t) { }
        };

        // Thrown on parent thread in a nested_exception when an unhandled exception occurs on a child thread.
        struct thread_exception : public std::exception
        {
            virtual const char* what() const noexcept override { return "Exception thrown from thread."; }
            const std::weak_ptr<detail::thread> task_ptr;
            thread_exception(const detail::thread_ptr& t) noexcept : task_ptr(t) { }
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
    }
}