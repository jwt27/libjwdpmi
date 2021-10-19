/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
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
#include <jw/main.h>

// TODO: task->delayed_start(), to schedule a task without immediately starting it.
// TODO: make errno thread-local? other globals?

namespace jw
{
    namespace thread
    {
        void yield();
        struct thread;

        namespace detail
        {
            struct scheduler
            {
                friend int ::main(int, const char**);
                friend void ::jw::thread::yield();
                friend struct ::jw::thread::thread;
                friend struct ::jw::init;

                template <typename T = std::byte>
                using allocator = monomorphic_allocator<dpmi::locked_pool_resource<true>, T>;

                static bool is_current_thread(const thread* t) noexcept { return instance->current_thread.get() == t; }
                static std::weak_ptr<thread> get_current_thread() noexcept { return instance->current_thread; }
                static auto get_current_thread_id() noexcept { return instance->current_thread->id; }
                static const auto& get_threads() { return instance->threads; }

                template<typename F>
                static void invoke_main(F&& function)
                {
                    if (is_current_thread(instance->main_thread.get()) and not dpmi::in_irq_context()) std::forward<F>(function)();
                    else instance->main_thread->invoke(std::forward<F>(function));
                }

                template<typename F>
                static void invoke_next(F&& function)
                {
                    if (not instance->threads.empty()) instance->threads.front()->invoke(std::forward<F>(function));
                    else if (dpmi::in_irq_context()) instance->current_thread->invoke(std::forward<F>(function));
                    else std::forward<F>(function)();
                }

                static auto* memory_resource() noexcept { return memres; }

                template<typename T = std::byte>
                static auto alloc() noexcept { return allocator<T> { memres }; }

            private:
                [[gnu::noinline, gnu::noclone, gnu::no_stack_limit, gnu::naked]] static void context_switch(thread_context**);
                static void thread_switch();
                static void start_thread(const thread_ptr&);
                [[gnu::noinline, gnu::cdecl]] static thread_context* set_next_thread();
                static void check_exception();
                [[gnu::force_align_arg_pointer, noreturn]] static void run_thread() noexcept;

                std::deque<thread_ptr, allocator<thread_ptr>> threads;
                thread_ptr current_thread;
                thread_ptr main_thread;
                bool terminating { false };

                static void setup();
                static void kill_all();
                scheduler();

                inline static constinit dpmi::locked_pool_resource<true>* memres { nullptr };
                inline static constinit scheduler* instance { nullptr };

                struct dtor { ~dtor(); } static inline destructor;
            };

            inline dpmi::locked_pool_resource<true>* scheduler_memres() { return scheduler::memory_resource(); }
        }
    }
}
