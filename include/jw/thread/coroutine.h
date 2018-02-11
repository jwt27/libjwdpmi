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
            template<typename sig>
            class coroutine_impl;

            template<typename R, typename... A>
            class coroutine_impl<R(A...)> : public task_base
            {
                template<typename> friend class coroutine;
                using this_t = coroutine_impl<R(A...)>;
                using base = task_base;

                std::function<void(A...)> function;
                std::unique_ptr<std::tuple<A...>> arguments;
                std::optional<R> result;
                bool result_available { false };

            protected:
                virtual void call() override { std::apply(function, *arguments); }

            public:
                // Start the coroutine thread using the specified arguments.
                template <typename... Args>
                constexpr void start(Args&&... args)
                {
                    if (this->is_running()) return; // or throw...?
                    arguments = std::make_unique<std::tuple<A...>>(std::forward<Args>(args)...);
                    result.reset();
                    base::start();
                }

                // Blocks until the coroutine yields a result, or terminates.
                // Returns true when it is safe to call await() to obtain the result.
                // May rethrow unhandled exceptions!
                bool try_await()
                {
                    dpmi::throw_if_irq();
                    if (scheduler::is_current_thread(this)) return false;

                    this->try_await_while([this]() { return this->is_running() and not result_available ; });

                    if (!result) return false;
                    return true;
                }

                // Awaits a result from the coroutine.
                // Throws illegal_await if the coroutine ends without yielding a result.
                // May rethrow unhandled exceptions!
                auto await()
                {
                    if (!try_await()) throw illegal_await(this->shared_from_this());

                    result_available = false;
                    this->state = running;
                    return std::move(*result);
                }

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

                template <typename F>
                coroutine_impl(F&& f, std::size_t stack_bytes = config::thread_default_stack_size) 
                    : base { stack_bytes }
                    , function { std::forward<F>(f) } { }
            };
        }

        template<typename Sig> struct coroutine : public detail::task_ptr<detail::coroutine_impl<Sig>>
        {
            template <typename... T>
            static void yield(T&&... value)
            {
                using namespace detail;
                if (auto* p = dynamic_cast<coroutine_impl<Sig>*>(scheduler::get_current_thread().lock().get()))
                    p->yield(std::forward<T>(value)...);
            }
        };

        //template<typename R, typename... A>
        //coroutine(R(*)(A...)) -> coroutine<R(A...)>;
        //
        //template<typename F, typename Sig = typename std::__function_guide_helper<decltype(&F::operator())>::type>
        //coroutine(F) -> coroutine<Sig>;
    }
}
