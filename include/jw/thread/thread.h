/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <exception>
#include <jw/thread/detail/scheduler.h>
#include <jw/main.h>
#include "jwdpmi_config.h"

namespace jw
{
    namespace thread
    {
        // Thrown when task->abort() is called.
        struct abort_thread
        {
            ~abort_thread() noexcept(false) { if (not defused) throw terminate_exception { }; }
            virtual const char* what() const noexcept { return "Thread aborted."; }
        private:
            friend struct detail::scheduler;
            void defuse() const noexcept { defused = true; }
            mutable bool defused { false };
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

        // Call a function on the next active thread.
        template<typename F> void invoke_next(F&& function) { detail::scheduler::invoke_next(std::forward<F>(function)); }

        struct thread
        {
            using id = std::uint32_t;
            using native_handle_type = std::weak_ptr<detail::thread>;

            thread() noexcept = default;
            template<typename F, typename... A>
            explicit thread(F&& f, A&&... args)
                : ptr { create(config::thread_default_stack_size, std::forward<F>(f), std::forward<A>(args)...) }
            { detail::scheduler::start_thread(ptr); }

            template<typename F, typename... A>
            explicit thread(std::size_t stack_size, F&& f, A&&... args)
                : ptr { create(stack_size, std::forward<F>(f), std::forward<A>(args)...) }
            { detail::scheduler::start_thread(ptr); }

            ~thread() noexcept(false) { if (ptr) terminate(); }

            thread(const thread&) = delete;
            thread& operator=(const thread&) = delete;
            thread(thread&&) noexcept = default;
            thread& operator=(thread&& other)
            {
                if (ptr) terminate();
                ptr = std::move(other.ptr);
                return *this;
            }

            void swap(thread& other) noexcept { using std::swap; swap(ptr, other.ptr); };
            bool joinable() const noexcept { return static_cast<bool>(ptr); }
            void join();
            void detach() { ptr.reset(); }
            id get_id() const noexcept { return ptr ? ptr->id : 0; };
            native_handle_type native_handle() { return ptr; };

            void abort() { ptr->abort(); };
            bool active() const noexcept { return ptr and ptr->active(); }

            static unsigned int hardware_concurrency() noexcept { return 1; }

        private:
            template<typename F, typename... A>
            auto create(std::size_t stack_size, F&& func, A&&... args);

            std::shared_ptr<detail::thread> ptr;
        };

        inline void thread::join()
        {
            if (not ptr)
                throw std::system_error { std::make_error_code(std::errc::no_such_process) };

            if (get_id() == detail::scheduler::get_current_thread_id())
                throw std::system_error { std::make_error_code(std::errc::resource_deadlock_would_occur) };

            yield_while([p = ptr.get()] { return p->active(); });
            ptr.reset();
        }

        template<typename F, typename... A>
        inline auto thread::create(std::size_t stack_size, F&& func, A&&... args)
        {
            static_assert(std::is_constructible_v<std::decay_t<F>, F>);
            static_assert(std::is_invocable_v<std::decay_t<F>, std::decay_t<A>...>);

            auto wrapper = [ func = std::tuple<F> { std::forward<F>(func) },
                             args = std::tuple<A...> { std::forward<A>(args)... } ]
                { std::apply(std::get<0>(func), args); };

            return detail::scheduler::create_thread(std::move(wrapper), stack_size);
        }
    }
}
