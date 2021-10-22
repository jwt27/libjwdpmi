/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <future>
#include <memory>
#include <variant>
#include <functional>
#include <jw/thread.h>

namespace jw::detail
{
    enum class promise_state { none, available, retrieved };
    template <typename T> struct promise_result_base
    {
        using actual_type = T;
        std::variant<std::monostate, std::exception_ptr, T> value { };
        promise_state state { promise_state::none };
    };

    template <typename R> struct promise_result : promise_result_base<R> { };
    template <typename R> struct promise_result<R&> : promise_result_base<std::reference_wrapper<R>> { };
    template <> struct promise_result<void> : promise_result_base<empty> { };

    struct construct_from_promise_t { } construct_from_promise;

    template <typename R>
    struct pf_base
    {
    protected:
        pf_base() noexcept = default;
        pf_base(std::allocator_arg_t) : result { std::make_shared<promise_result<R>>() } { }
        template<typename Alloc>
        pf_base(std::allocator_arg_t, const Alloc& a) : result { std::allocate_shared<promise_result<R>>(a) } { }

        pf_base(const pf_base&) = default;
        pf_base(pf_base&&) noexcept = default;
        pf_base& operator=(const pf_base&) = default;
        pf_base& operator=(pf_base&&) noexcept = default;

        std::shared_ptr<promise_result<R>> result;
    };

    template <typename R>
    struct promise_base;

    template <typename R>
    struct future_base : pf_base<R>
    {
        using base = pf_base<R>;

        future_base() noexcept = default;

        future_base(const future_base&) = delete;
        future_base(future_base&&) noexcept = default;
        future_base& operator=(const future_base&) = delete;
        future_base& operator=(future_base&&) noexcept = default;

        future_base(const base&) = delete;
        future_base(base&&) = delete;
        future_base& operator=(const base&) = delete;
        future_base& operator=(base&&) = delete;

        bool valid() const noexcept { return static_cast<bool>(base::result); }

        void wait() const
        {
            this_thread::yield_while([&s = this->result.get()->state] { return s == promise_state::none; });
        };

        template<class Rep, class Period>
        std::future_status wait_for(const std::chrono::duration<Rep, Period>& rel_time) const
        {
            auto timeout = this_thread::yield_while_for([&s = this->result.get()->state] { return s == promise_state::none; }, rel_time);
            if (timeout) return std::future_status::timeout;
            return std::future_status::ready;
        }

        template<class Clock, class Duration>
        std::future_status wait_until(const std::chrono::time_point<Clock, Duration>& abs_time) const
        {
            auto timeout = this_thread::yield_while_until([&s = this->result.get()->state] { return s == promise_state::none; }, abs_time);
            if (timeout) return std::future_status::timeout;
            return std::future_status::ready;
        }

    protected:
        promise_result<R> get_result()
        {
            wait();
            auto result = std::move(*this->result);
            this->result->value = { };
            this->result->state = promise_state::retrieved;
            this->result.reset();
            if (auto* e = std::get_if<std::exception_ptr>(&result.value))
                std::rethrow_exception(*e);
            return result;
        }

        friend struct promise_base<R>;

        future_base(const promise_base<R>& p) : base { p } { }
    };
}

namespace jw
{
    template <typename R>
    struct future : detail::future_base<R>
    {
        R get() { return std::get<2>(this->get_result().value); }
    };

    template <typename R>
    struct future<R&> : detail::future_base<R&>
    {
        R& get() { return std::get<2>(this->get_result().value).get(); };
    };

    template <>
    struct future<void> : detail::future_base<void>
    {
        void get() { this->get_result(); };
    };
}
namespace jw::detail
{
    template <typename R>
    struct promise_base : pf_base<R>
    {
        using base = pf_base<R>;

        promise_base() : base { std::allocator_arg } { }
        template <class Alloc>
        promise_base(std::allocator_arg_t, const Alloc& alloc) : base { std::allocator_arg, alloc } { }

        promise_base(promise_base&& other) noexcept = default;
        promise_base(const promise_base& other) = delete;
        promise_base& operator=(promise_base&& rhs) noexcept = default;
        promise_base& operator=(const promise_base&) = delete;

        promise_base(const base&) = delete;
        promise_base(base&&) = delete;
        promise_base& operator=(const base&) = delete;
        promise_base& operator=(base&&) = delete;

        ~promise_base()
        {
            if (not this->result) return;
            if (this->result->value.index() != 0) return;
            set(std::make_exception_ptr(std::future_error { std::future_errc::broken_promise }));
        }

        void swap(promise_base& other) noexcept { using std::swap; swap(this->result, other.result); };

        future<R> get_future()
        {
            if (not this->result) throw std::future_error { std::future_errc::no_state };
            if (this->result.use_count() > 1) throw std::future_error { std::future_errc::future_already_retrieved };
            return future<R> { *this };
        }

        void set_exception(std::exception_ptr p) { check(); this->result.value = p; set(); }
        void set_exception_at_thread_exit(std::exception_ptr p) { check(); this->result.value = p; set_atexit(); }

    protected:
        template<typename T>
        void set(T&& value)
        {
            check();
            this->result->value = std::forward<T>(value);
            this->result->state = promise_state::available;
        }

        template<typename T>
        void set_atexit(T&& value)
        {
            check();
            this->result->value = std::forward<T>(value);
            scheduler::current_thread()->atexit([&s = this->result->state] { s = promise_state::available; });
        }

        void check()
        {
            if (not this->result) throw std::future_error { std::future_errc::no_state };
            if (this->result->value.index() != 0) throw std::future_error { std::future_errc::promise_already_satisfied };
        }
    };
}

namespace jw
{
    template <typename R>
    struct promise : detail::promise_base<R>
    {
        void set_value(const R& v) { this->template set(v); }
        void set_value(R&& v) { this->template set(std::move(v)); }
        void set_value_at_thread_exit(const R& v) { this->template set_atexit(v); }
        void set_value_at_thread_exit(R&& v) { this->template set_atexit(std::move(v)); }
    };

    template <typename R>
    struct promise<R&> : detail::promise_base<R&>
    {
        void set_value(R& v) { this->template set(std::reference_wrapper<R>{ v }); }
        void set_value_at_thread_exit(const R& v) { this->template set_atexit(std::reference_wrapper<R>{ v }); }
    };

    template <>
    struct promise<void> : detail::promise_base<empty>
    {
        void set_value() { this->template set(empty { }); }
        void set_value_at_thread_exit() { this->template set_atexit(empty { }); }
    };

    template <typename R>
    void swap(promise<R>& x, promise<R>& y) noexcept { x.swap(y); }
}

namespace std
{
    template <typename R, typename Alloc>
    struct uses_allocator<jw::promise<R>, Alloc> : std::true_type { };
}
