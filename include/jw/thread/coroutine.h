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
#include <jw/thread/task.h>

namespace jw
{
    namespace thread
    {
        namespace detail
        {
            template<typename sig, std::size_t stack_bytes>
            class coroutine_impl;

            template<typename R, typename... A, std::size_t stack_bytes>
            class coroutine_impl<R(A...), stack_bytes> : public task_base<stack_bytes>
            {
                template<typename, std::size_t> friend class coroutine;
                using this_t = coroutine_impl<R(A...), stack_bytes>;
                using base = task_base<stack_bytes>;

                std::function<void(this_t&, A...)> function;
                std::unique_ptr<std::tuple<this_t&, A...>> arguments;
                std::unique_ptr<R> result;

            protected:
                virtual void call() override { std::apply(function, *arguments); }

            public:
                // Start the coroutine thread using the specified arguments.
                template <typename... Args>
                constexpr void start(Args&&... args)
                {
                    if (this->is_running()) return; // or throw...?
                    arguments = std::make_unique<std::tuple<this_t&, A...>>(*this, std::forward<Args>(args)...);
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

                    this->try_await_while([this]() { return this->state == running || (this->state == suspended && !result); });

                    if (this->state != suspended) return false;
                    if (!result) return false;
                    return true;
                }

                // Awaits a result from the coroutine.
                // Throws illegal_await if the coroutine ends without yielding a result.
                // May rethrow unhandled exceptions!
                auto await()
                {
                    if (!try_await()) throw illegal_await(this->shared_from_this());

                    this->state = running;
                    return std::move(*result);
                }

                // Called by the coroutine thread to yield a result.
                // This suspends the coroutine until the result is obtained by calling await().
                template <typename T>
                void yield(T&& value)
                {
                    if (!scheduler::is_current_thread(this)) return; // or throw?

                    result = std::make_unique<R>(std::forward<T>(value));
                    this->state = suspended;
                    ::jw::thread::yield();
                    result.reset();
                }

                template <typename F>
                coroutine_impl(F&& f) : function { std::forward<F>(f) } { }
            };
        }

        template<typename sig, std::size_t stack_bytes = config::thread_default_stack_size>
        class coroutine;

        template<typename R, typename... A, std::size_t stack_bytes>
        class coroutine<R(A...), stack_bytes>
        {
            using task_type = detail::coroutine_impl<R(A...), stack_bytes>;
            std::shared_ptr<task_type> ptr;

        public:
            constexpr const auto get_ptr() const noexcept { return ptr; }
            constexpr auto* operator->() const { return ptr.get(); }
            constexpr auto& operator*() const { return *ptr; }
            constexpr operator bool() const { return ptr.operator bool(); }

            template<typename F>
            constexpr coroutine(F&& f) : ptr(std::make_shared<task_type>(std::forward<F>(f))) { }

            constexpr coroutine(const coroutine&) = default;
            constexpr coroutine() = default;
        };
    }
}
