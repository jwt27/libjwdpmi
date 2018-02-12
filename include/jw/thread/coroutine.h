/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/thread/task.h>

namespace jw
{
    namespace thread
    {
        namespace detail
        {
            template<typename R>
            class coroutine_base : public task_base
            {
            protected:
                coroutine_base(std::size_t stack_bytes) : task_base { stack_bytes } { }

                std::optional<R> result;
                bool result_available { false };

            public:
                // Called by the coroutine thread to yield a result.
                // This suspends the coroutine until the result is obtained by calling await().
                template <typename... T>
                void yield(T&&... value)
                {
                    if (!scheduler::is_current_thread(this)) return; // or throw?

                    result = std::make_optional<R>(std::forward<T>(value)...);
                    result_available = true;
                    this->state = suspended;
                    ::jw::thread::yield_while([this] { return result_available; });
                    result.reset();
                }
            };

            template<typename sig>
            class coroutine_impl;

            template<typename R, typename... A>
            class coroutine_impl<R(A...)> : public coroutine_base<R>
            {
                using this_t = coroutine_impl<R(A...)>;
                using base = coroutine_base<R>;

                std::function<void(A...)> function;
                std::unique_ptr<std::tuple<A...>> arguments;

            protected:
                virtual void call() override { std::apply(function, *arguments); }

            public:
                // Start the coroutine thread using the specified arguments.
                template <typename... Args>
                constexpr void start(Args&&... args)
                {
                    if (this->is_running()) return; // or throw...?
                    arguments = std::make_unique<std::tuple<A...>>(std::forward<Args>(args)...);
                    this->result.reset();
                    base::start();
                }

                // Blocks until the coroutine yields a result, or terminates.
                // Returns true when it is safe to call await() to obtain the result.
                // May rethrow unhandled exceptions!
                bool try_await()
                {
                    dpmi::throw_if_irq();
                    if (scheduler::is_current_thread(this)) return false;

                    this->try_await_while([this]() { return this->is_running() and not this->result_available ; });

                    if (not this->result) return false;
                    return true;
                }

                // Awaits a result from the coroutine.
                // Throws illegal_await if the coroutine ends without yielding a result.
                // May rethrow unhandled exceptions!
                decltype(auto) await()
                {
                    if (!try_await()) throw illegal_await(this->shared_from_this());

                    this->result_available = false;
                    this->state = running;
                    return std::move(*(this->result));
                }

                template <typename F>
                coroutine_impl(F&& f, std::size_t stack_bytes = config::thread_default_stack_size) 
                    : base { stack_bytes }
                    , function { std::forward<F>(f) } { }
            };
        }
        template<typename R, typename sig> struct coroutine;

        template<typename R, typename... A> struct coroutine<R, void(A...)> : public detail::task_ptr<detail::coroutine_impl<R(A...)>> { };

        template <typename R, typename... T>
        static void coroutine_yield(T&&... value)
        {
            using namespace detail;
            if (auto* p = dynamic_cast<coroutine_base<R>*>(scheduler::get_current_thread().lock().get()))
                p->yield(std::forward<T>(value)...);
        }
        template <typename R>
        static void coroutine_yield(R&& value) { return coroutine_yield<R>(std::forward<R>(value)); }

        //template<typename R, typename... A>
        //coroutine(void(*)(A...)) -> coroutine<R, void(A...)>;
        //
        //template<typename R, typename F, typename Sig = typename std::__function_guide_helper<decltype(&F::operator())>::type>
        //coroutine(F) -> coroutine<R, Sig>;

        template<typename R, typename... A>
        auto make_coroutine(void(*f)(A...)) { return coroutine<R, void(A...)> { f }; }

        template<typename R, typename F, typename Sig = typename std::__function_guide_helper<decltype(&F::operator())>::type>
        auto make_coroutine(F&& f) { return coroutine<R, Sig> { std::forward<F>(f) }; }
    }
}
