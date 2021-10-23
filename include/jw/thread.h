/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <exception>
#include <jw/detail/scheduler.h>
#include <jw/main.h>
#include "jwdpmi_config.h"

namespace jw
{
    struct deadlock : std::system_error
    {
        deadlock() : system_error { std::make_error_code(std::errc::resource_deadlock_would_occur) } { }
    };

    struct thread
    {
        using id = detail::scheduler::thread_id;
        using native_handle_type = detail::thread*;

        thread() noexcept = default;
        template<typename F, typename... A>
        explicit thread(F&& f, A&&... args)
            : ptr { create(config::thread_default_stack_size, std::forward<F>(f), std::forward<A>(args)...) }
        {
            detail::scheduler::start_thread(ptr);
        }

        template<typename F, typename... A>
        explicit thread(std::size_t stack_size, F&& f, A&&... args)
            : ptr { create(stack_size, std::forward<F>(f), std::forward<A>(args)...) }
        {
            detail::scheduler::start_thread(ptr);
        }

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
        native_handle_type native_handle() { return ptr.get(); };

        void abort() { ptr->abort(); };
        bool active() const noexcept { return ptr and ptr->active(); }

        template<typename T>
        void name(T&& string) { ptr->set_name(std::forward<T>(string)); }
        std::string_view name() { return ptr->get_name(); }

        static unsigned int hardware_concurrency() noexcept { return 1; }

    private:
        template<typename F, typename... A>
        auto create(std::size_t stack_size, F&& func, A&&... args);

        std::shared_ptr<detail::thread> ptr;
    };

    inline void swap(thread& a, thread& b) noexcept { a.swap(b); }
}

namespace jw::this_thread
{
    inline jw::thread::id get_id() noexcept { return detail::scheduler::current_thread_id(); }

    // Yields execution to the next thread in the queue.
    inline void yield()
    {
        detail::scheduler::yield();
    }

    // Yields execution while the given condition evaluates to true.
    template<typename F>
    inline void yield_while(F&& condition)
    {
        while (condition()) yield();
    };

    // Yields execution until the given time point.
    template<typename P>
    inline void yield_until(const P& time_point)
    {
        yield_while([&time_point] { return P::clock::now() < time_point; });
    };

    // Yields execution for the given duration.
    template<typename C = config::thread_clock>
    inline void yield_for(const typename C::duration& duration)
    {
        yield_until(C::now() + duration);
    };

    // Combination of yield_while() and yield_until(). Returns true on timeout.
    template<typename F, typename P>
    inline bool yield_while_until(F&& condition, const P& time_point)
    {
        bool c;
        yield_while([&] { return (c = condition()) and P::clock::now() < time_point; });
        return c;
    };

    // Combination of yield_while() and yield_for(). Returns true on timeout.
    template<typename C = config::thread_clock, typename F>
    inline bool yield_while_for(F&& condition, const typename C::duration& duration)
    {
        return yield_while_until(condition, C::now() + duration);
    };

    inline void sleep() { return yield(); }
    template<typename F>
    inline void sleep_while(F&& condition) { return yield_while(std::forward<F>(condition)); };
    template<typename P>
    inline void sleep_until(const P& time_point) { return yield_until(time_point); };
    template<typename C = config::thread_clock>
    inline void sleep_for(const typename C::duration& duration) { return yield_for(duration); };
    template<typename F, typename P>
    inline bool sleep_while_until(F&& condition, const P& time_point) { return yield_while_until(std::forward<F>(condition)); };
    template<typename C = config::thread_clock, typename F>
    inline bool sleep_while_for(F&& condition, const typename C::duration& duration) { return yield_while_for(std::forward<F>(condition), duration); };

    // Call a function on the main thread.
    template<typename F>
    void invoke_main(F&& function) { detail::scheduler::invoke_main(std::forward<F>(function)); }

    // Call a function on the next active thread.
    template<typename F>
    void invoke_next(F&& function) { detail::scheduler::invoke_next(std::forward<F>(function)); }
}

namespace jw
{
    inline void thread::join()
    {
        if (not ptr) throw std::system_error { std::make_error_code(std::errc::no_such_process) };
        if (get_id() == detail::scheduler::current_thread_id()) throw deadlock { };
        this_thread::yield_while([p = ptr.get()] { return p->active(); });
        ptr.reset();
    }

    template<typename F, typename... A>
    inline auto thread::create(std::size_t stack_size, F&& func, A&&... args)
    {
        static_assert(std::is_invocable_v<std::decay_t<F>, std::decay_t<A>...>);

        auto wrapper = callable_tuple { std::forward<F>(func), std::forward<A>(args)... };
        return detail::scheduler::create_thread(std::move(wrapper), stack_size);
    }
}
