/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

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

int main(int, char**);

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
                friend int ::main(int, char**);
                static dpmi::locked_pool_allocator<> alloc;
                static std::deque<thread_ptr, dpmi::locked_pool_allocator<>> threads;
                static thread_ptr current_thread;
                static thread_ptr main_thread;

            public:
                static bool is_current_thread(const thread* t) noexcept { return current_thread.get() == t; }
                static std::weak_ptr<thread> get_current_thread() noexcept { return current_thread; }
                static auto& get_current_thread_id() noexcept { return current_thread->id(); }
                static const auto& get_threads() { return threads; }

            private:
                [[gnu::noinline, gnu::noclone, gnu::no_stack_limit]] static void context_switch() noexcept;
                static void thread_switch(thread_ptr = nullptr);
                [[gnu::noinline]] static void set_next_thread() noexcept;
                static void check_exception();

                [[gnu::used]] static void run_thread() noexcept;

                struct init_main { init_main(); } static initializer;
            };
        }
    }
}
