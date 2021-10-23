/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <future>
#include <memory>
#include <variant>
#include <functional>
#include <memory>
#include <jw/thread.h>

namespace jw::detail
{
    template <typename T>
    struct promise_result_base
    {
        using actual_type = T;

        template<typename U>
        void set_value(U&& v)
        {
            if (has_result()) throw std::future_error { std::future_errc::promise_already_satisfied };
            new (&value) T { std::forward<U>(v) };
            i = index::value;
        }

        template<typename U>
        void set_exception(U&& v)
        {
            if (has_result()) throw std::future_error { std::future_errc::promise_already_satisfied };
            new (&exception) std::exception_ptr { std::forward<U>(v) };
            i = index::exception;
        }

        void make_ready() noexcept { ready = true; }
        bool is_ready() noexcept { return ready; }
        bool has_result() noexcept { return i != index::none; }

        T move_result()
        {
            if (i == index::exception) std::rethrow_exception(std::move(exception));
            return std::move(value);
        }

        T& share_result()
        {
            if (i == index::exception) std::rethrow_exception(exception);
            return (value);
        }

        promise_result_base() noexcept { };

        ~promise_result_base()
        {
            if (i == index::exception) exception.~exception_ptr();
            if (i == index::value) value.~T();
        }

    private:
        union
        {
            std::exception_ptr exception;
            T value;
        };
        enum class index : std::uint8_t
        {
            none,
            exception,
            value
        } i { index::none };
        bool ready;
    };

    template <typename R> struct promise_result : promise_result_base<R> { };
    template <typename R> struct promise_result<R&> : promise_result_base<std::reference_wrapper<R>> { };
    template <> struct promise_result<void> : promise_result_base<empty> { };

    template <typename R>
    struct pf_base
    {
    protected:
        pf_base() noexcept = default;
        pf_base(std::allocator_arg_t) : shared_state { allocate(std::allocator<promise_result<R>> { }) } { }
        template<typename Alloc>
        pf_base(std::allocator_arg_t, const Alloc& a) : shared_state { allocate(a) } { }
        pf_base(std::shared_ptr<promise_result<R>>&& r) : shared_state { std::move(r) } { }

        pf_base(const pf_base&) noexcept = default;
        pf_base(pf_base&&) noexcept = default;
        pf_base& operator=(const pf_base&) noexcept = default;
        pf_base& operator=(pf_base&&) noexcept = default;

        bool valid() const noexcept { return static_cast<bool>(shared_state); }

        void wait() const
        {
            this_thread::yield_while([s = state()] { return not s->has_result(); });
        };

        template<class Rep, class Period>
        std::future_status wait_for(const std::chrono::duration<Rep, Period>& rel_time) const
        {
            auto timeout = this_thread::yield_while_for([s = state()] { return not s->has_result(); }, rel_time);
            if (timeout) return std::future_status::timeout;
            return std::future_status::ready;
        }

        template<class Clock, class Duration>
        std::future_status wait_until(const std::chrono::time_point<Clock, Duration>& abs_time) const
        {
            auto timeout = this_thread::yield_while_until([s = state()] { return not s->has_result(); }, abs_time);
            if (timeout) return std::future_status::timeout;
            return std::future_status::ready;
        }

        promise_result<R>* state() const
        {
            if (not valid()) throw std::future_error { std::future_errc::no_state };
            return shared_state.get();
        }

        void reset() noexcept { shared_state.reset(); }
        std::shared_ptr<promise_result<R>> move_state() noexcept { return std::move(shared_state); }
        const std::shared_ptr<promise_result<R>>& copy_state() const noexcept { return (shared_state); }

    private:
        template<typename A>
        static std::shared_ptr<promise_result<R>> allocate(const A& alloc)
        {
            using rebind = typename std::allocator_traits<A>::rebind_alloc<promise_result<R>>;
            using deleter = allocator_delete<rebind>;
            rebind r { alloc };
            auto* p = std::allocator_traits<rebind>::allocate(r, 1);
            p = new(p) promise_result<R> { };
            return { p, deleter { r }, r };
        }

        std::shared_ptr<promise_result<R>> shared_state;
    };
}

namespace jw
{
    template <typename R> struct future;
    template <typename R> struct shared_future;
}

namespace jw::detail
{
    template <typename R> struct promise_base;
    template <typename R> struct shared_future_base;

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

        shared_future<R> share() noexcept;

        using base::valid;
        using base::wait;
        using base::wait_for;
        using base::wait_until;

    protected:
        promise_result<R>::actual_type get_result()
        {
            wait();
            local_destructor do_reset { [this] { this->reset(); } };
            return this->state()->move_result();
        }

        friend struct promise_base<R>;
        friend struct shared_future_base<R>;

        future_base(const promise_base<R>& p) : base { p } { }
    };

    template <typename R>
    struct shared_future_base : pf_base<R>
    {
        using base = pf_base<R>;

        shared_future_base() noexcept = default;

        shared_future_base(const shared_future_base&) noexcept = default;
        shared_future_base(shared_future_base&&) noexcept = default;
        shared_future_base& operator=(const shared_future_base&) = default;
        shared_future_base& operator=(shared_future_base&&) noexcept = default;

        shared_future_base(const base&) = delete;
        shared_future_base(base&&) = delete;
        shared_future_base& operator=(const base&) = delete;
        shared_future_base& operator=(base&&) = delete;

        shared_future_base(future_base<R>&& f) noexcept : base { std::move(f.move_state()) } { }

        using base::valid;
        using base::wait;
        using base::wait_for;
        using base::wait_until;

    protected:
        promise_result<R>::actual_type& get_result() const
        {
            wait();
            return this->state()->share_result();
        }
    };
}

namespace jw
{
    template <typename R>
    struct future : detail::future_base<R>
    {
        R get() { return this->get_result(); }
    };

    template <typename R>
    struct future<R&> : detail::future_base<R&>
    {
        R& get() { return this->get_result().get(); };
    };

    template <>
    struct future<void> : detail::future_base<void>
    {
        void get() { this->get_result(); };
    };

    template <typename R>
    struct shared_future : detail::shared_future_base<R>
    {
        const R& get() const { return this->get_result(); }
    };

    template <typename R>
    struct shared_future<R&> : detail::shared_future_base<R&>
    {
        R& get() const { return this->get_result().get(); };
    };

    template <>
    struct shared_future<void> : detail::shared_future_base<void>
    {
        void get() const { this->get_result(); };
    };
}
namespace jw::detail
{
    template<typename R>
    shared_future<R> future_base<R>::share() noexcept { return shared_future<R> { std::move(*this) }; }

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
            if (not this->valid()) return;
            if (this->state()->has_result()) return;
            set_exception(std::make_exception_ptr(std::future_error { std::future_errc::broken_promise }));
        }

        void swap(promise_base& other) noexcept { using std::swap; swap(this->result, other.result); };

        future<R> get_future()
        {
            this->state();
            if (future_retrieved) throw std::future_error { std::future_errc::future_already_retrieved };
            future_retrieved = true;
            return future<R> { *this };
        }

        void set_exception(std::exception_ptr e)
        {
            auto state = this->state();
            state->set_exception(std::move(e));
            state->make_ready();
        }
        void set_exception_at_thread_exit(std::exception_ptr e)
        {
            auto state = this->state();
            state->set_exception(std::move(e));
            make_ready_atexit();
        }

    protected:
        template<typename T>
        void set(T&& value)
        {
            auto state = this->state();
            state->set_value(std::forward<T>(value));
            state->make_ready();
        }

        template<typename T>
        void set_atexit(T&& value)
        {
            auto state = this->state();
            state->set_value(std::forward<T>(value));
            make_ready_atexit();
        }

    private:
        void make_ready_atexit() { scheduler::current_thread()->atexit([s = this->copy_state()] { s->make_ready(); }); }

        bool future_retrieved { false };
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
    struct promise<void> : detail::promise_base<void>
    {
        void set_value() { this->template set(empty { }); }
        void set_value_at_thread_exit() { this->template set_atexit(empty { }); }
    };

    template <typename R>
    void swap(promise<R>& x, promise<R>& y) noexcept { x.swap(y); }
}

namespace jw::detail
{
    template <typename F, typename... A>
    [[nodiscard]] auto do_async(std::launch, F&& func, A&&... args)
    {
        using result = std::invoke_result_t<std::decay_t<F>, std::decay_t<A>...>;
        callable_tuple call { std::forward<F>(func), std::forward<A>(args)... };
        promise<result> p { };
        auto f = p.get_future();
        jw::thread t { [call = std::move(call), p = std::move(p)] () mutable
        {
            try
            {
                if constexpr (std::is_void_v<result>) { call(); p.set_value(); }
                else p.set_value(call());
            }
            catch (...) { p.set_exception(std::current_exception()); }
        } };
        t.detach();
        return f;
    }
}

namespace jw
{
    template <typename F, typename... A>
    [[nodiscard]] auto async(std::launch policy, F&& f, A&&... args) { return detail::do_async(policy, std::forward<F>(f), std::forward<A>(args)...); }

    template <typename F, typename... A>
    [[nodiscard]] auto async(F&& f, A&&... args) { return detail::do_async(std::launch::async, std::forward<F>(f), std::forward<A>(args)...); }
}

namespace std
{
    template <typename R, typename Alloc>
    struct uses_allocator<jw::promise<R>, Alloc> : std::true_type { };
}
