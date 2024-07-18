/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2016 - 2024 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <exception>
#include <stop_token>
#include <concepts>
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
        using id = detail::thread_id;
        using native_handle_type = detail::thread*;

        thread() noexcept = default;
        template<typename F, typename... A> requires std::invocable<std::decay_t<F>, std::decay_t<A>...>
        explicit thread(F&& f, A&&... args)
            : thread { config::thread_default_stack_size, std::forward<F>(f), std::forward<A>(args)... } { }

        template<typename F, typename... A> requires std::invocable<std::decay_t<F>, std::decay_t<A>...>
        explicit thread(std::size_t stack_size, F&& f, A&&... args)
            : ptr { create(stack_size, std::forward<F>(f), std::forward<A>(args)...) } { }

        ~thread() noexcept(false) { if (ptr) terminate(); }

        thread(const thread&) = delete;
        thread& operator=(const thread&) = delete;
        thread(thread&&) noexcept = default;
        thread& operator=(thread&& other)
        {
            if (ptr) terminate();
            ptr = std::move(other.ptr);
            other.ptr = nullptr;
            return *this;
        }

        void swap(thread& other) noexcept { using std::swap; swap(ptr, other.ptr); };
        [[nodiscard]] bool joinable() const noexcept { return ptr != nullptr; }
        void join();
        void detach() { ptr->detach(); ptr = nullptr; }
        [[nodiscard]] id get_id() const noexcept { return ptr ? ptr->id : 0; };
        [[nodiscard]] native_handle_type native_handle() { return ptr; };

        void cancel() { ptr->cancel(); };
        [[nodiscard]] bool active() const noexcept { return ptr and ptr->active(); }

        template<typename F>
        void invoke(F&& func) { ptr->invoke(std::forward<F>(func)); }

        template<typename T>
        void name(T&& string) { ptr->set_name(std::forward<T>(string)); }
        std::string_view name() { return ptr->get_name(); }

        [[nodiscard]] static unsigned int hardware_concurrency() noexcept { return 1; }

    private:
        template<typename F, typename... A>
        detail::thread* create(std::size_t, F&&, A&&...);

        detail::thread* ptr;
    };

    inline void swap(thread& a, thread& b) noexcept { a.swap(b); }

    struct jthread
    {
        using id = thread::id;
        using native_handle_type = thread::native_handle_type;

        jthread() noexcept : stop { std::nostopstate }, t { } { }

        template<typename F, typename... A>
        explicit jthread(F&& f, A&&... args)
            requires (std::invocable<std::decay_t<F>, std::stop_token, std::decay_t<A>...>
                      or std::invocable<std::decay_t<F>, std::decay_t<A>...>)
            : jthread { config::thread_default_stack_size, std::forward<F>(f), std::forward<A>(args)... } { }

        template<typename F, typename... A>
        explicit jthread(std::size_t stack_size, F&& f, A&&... args)
            : stop { }, t { create(stop, stack_size, std::forward<F>(f), std::forward<A>(args)...) } { }

        ~jthread() { if (joinable()) { request_stop(); join(); } }

        jthread(const jthread&) = delete;
        jthread& operator=(const jthread&) = delete;
        jthread(jthread&&) noexcept = default;
        jthread& operator=(jthread&& other) noexcept
        {
            this->~jthread();
            return *new(this) jthread { std::move(other) };
        }

        void swap(jthread& other) noexcept { using std::swap; swap(t, other.t); swap(stop, other.stop); }
        [[nodiscard]] bool joinable() const noexcept { return t.joinable(); }
        void join() { return t.join(); }
        void detach() { return t.detach(); }
        [[nodiscard]] id get_id() const noexcept { return t.get_id(); }
        [[nodiscard]] native_handle_type native_handle() { return t.native_handle(); }

        void cancel() { return t.cancel(); };
        [[nodiscard]] bool active() const noexcept { return t.active(); }

        template<typename F>
        void invoke(F&& func) { t.invoke(std::forward<F>(func)); }

        template<typename T>
        void name(T&& string) { t.name(std::forward<T>(string)); }
        std::string_view name() { return t.name(); }

        [[nodiscard]] std::stop_source get_stop_source() noexcept { return stop; }
        [[nodiscard]] std::stop_token get_stop_token() const noexcept { return stop.get_token(); }
        bool request_stop() noexcept { return stop.request_stop(); }

        [[nodiscard]] static unsigned int hardware_concurrency() noexcept { return thread::hardware_concurrency(); }

    private:
        template<typename F, typename... A>
        thread create(std::stop_source&, std::size_t, F&&, A&&...);

        std::stop_source stop;
        thread t;
    };

    inline void swap(jthread& a, jthread& b) noexcept { a.swap(b); }
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
    }

    // Yields execution until the given time point.
    template<typename P>
    inline void yield_until(const P& time_point)
    {
        yield_while([&time_point] { return P::clock::now() < time_point; });
    }

    // Yields execution for the given duration.
    template<typename C = config::thread_clock>
    inline void yield_for(const typename C::duration& duration)
    {
        yield_until(C::now() + duration);
    }

    // Combination of yield_while() and yield_until(). Returns true on timeout.
    template<typename F, typename P>
    inline bool yield_while_until(F&& condition, const P& time_point)
    {
        bool c;
        yield_while([&] { return (c = condition()) and P::clock::now() < time_point; });
        return c;
    }

    // Combination of yield_while() and yield_for(). Returns true on timeout.
    template<typename C = config::thread_clock, typename F>
    inline bool yield_while_for(F&& condition, const typename C::duration& duration)
    {
        return yield_while_until(condition, C::now() + duration);
    }

    inline void sleep() { return yield(); }
    template<typename F>
    inline void sleep_while(F&& condition) { return yield_while(std::forward<F>(condition)); }
    template<typename P>
    inline void sleep_until(const P& time_point) { return yield_until(time_point); }
    template<typename C = config::thread_clock>
    inline void sleep_for(const typename C::duration& duration) { return yield_for(duration); }
    template<typename F, typename P>
    inline bool sleep_while_until(F&& condition, const P& time_point) { return yield_while_until(std::forward<F>(condition)); }
    template<typename C = config::thread_clock, typename F>
    inline bool sleep_while_for(F&& condition, const typename C::duration& duration) { return yield_while_for(std::forward<F>(condition), duration); }

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
        auto id = ptr->id;
        ptr->resume();
        detach();
        this_thread::yield_while([id] { return detail::scheduler::get_thread(id) != nullptr; });
    }

    template<typename F, typename... A>
    inline detail::thread* thread::create(std::size_t stack_size, F&& func, A&&... args)
    {
        auto wrapper = callable_tuple { std::forward<F>(func), std::forward<A>(args)... };
        return detail::scheduler::create_thread(std::move(wrapper), stack_size);
    }

    template<typename F, typename... A>
    inline thread jthread::create(std::stop_source& s, std::size_t stack_size, F&& func, A&&... args)
    {
        if constexpr (std::is_invocable_v<std::decay_t<F>, std::stop_token, std::decay_t<A>...>)
            return thread { stack_size, std::forward<F>(func), s.get_token(), std::forward<A>(args)... };
        else
            return thread { stack_size, std::forward<F>(func), std::forward<A>(args)... };
    }
}
