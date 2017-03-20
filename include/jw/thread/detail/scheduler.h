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
#include <iostream>
#include <functional>
#include <memory>
#include <deque> 
#include <jw/thread/detail/thread.h>
#include <jw/dpmi/irq_check.h>
#include <jw/dpmi/alloc.h>

// TODO: task->delayed_start(), to schedule a task without immediately starting it.
// TODO: make errno thread-local? other globals?

namespace jw
{
    namespace thread
    {
        void yield();

        namespace detail
        {
            class scheduler
            {
                template<std::size_t> friend class task_base;
                friend void ::jw::thread::yield();
                static dpmi::locked_pool_allocator<> alloc;
                static std::deque<thread_ptr, dpmi::locked_pool_allocator<>> threads;
                static thread_ptr current_thread;
                static thread_ptr main_thread;

            public:
                static bool is_current_thread(const thread* t) noexcept { return current_thread.get() == t; }
                static std::weak_ptr<thread> get_current_thread() noexcept { return current_thread; }
                static const auto& get_threads() { return threads; }

            private:
                [[gnu::noinline, gnu::noclone, gnu::no_stack_limit, gnu::hot]] static void context_switch() noexcept;
                [[gnu::hot]] static void thread_switch(thread_ptr = nullptr);
                [[gnu::noinline, hot]] static void set_next_thread() noexcept;
                [[gnu::hot]] static void check_exception();                      

                [[gnu::used]] static void run_thread() noexcept;

                struct init_main { init_main(); } static initializer;
            };
        }
    }
}
