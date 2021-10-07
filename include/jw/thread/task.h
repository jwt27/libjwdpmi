/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/thread/detail/thread.h>
#include <jw/thread/detail/scheduler.h>
#include <jw/thread/thread.h>
#include "jwdpmi_config.h"

namespace jw
{
    namespace thread
    {
        namespace detail
        {
            class task_base : public detail::thread, public std::enable_shared_from_this<task_base>
            {
            protected:
                task_base(std::size_t stack_bytes) : thread(stack_bytes) { }

                void start()
                {
                    if (this->is_running()) return;
                    if (exceptions.size() > 0) try_await_while([this] { return true; });

                    this->state = starting;
                    if (dpmi::in_irq_context()) this->parent = scheduler::main_thread;
                    else this->parent = scheduler::current_thread;
                    scheduler::start_thread(this->shared_from_this());
                }

                void try_await_while(auto f)
                {
                    struct await_lock
                    {
                        await_lock(std::shared_ptr<task_base> me) { scheduler::current_thread->awaiting = me; }
                        ~await_lock() { scheduler::current_thread->awaiting.reset(); }
                    } lock { this->shared_from_this() };

                    do { yield(); } while (f());
                }

            public:
                // --- Public functions and variables inherited from detail::thread:
                // bool is_running() const noexcept;
                // std::size_t pending_exceptions() const noexcept;
                // std::uint32_t id() const noexcept;
                // thread_state get_state() const noexcept;
                // void suspend() noexcept;
                // void resume() noexcept;
                // void invoke(F&& function)
                // std::string name;
                // bool allow_orphan;

                // Aborts the task.
                // This throws an abort_thread exception on the thread, allowing the task to clean up and return normally.
                // May rethrow unhandled exceptions!
                virtual void abort(bool wait = true) override
                {
                    detail::thread::abort();

                    if (dpmi::in_irq_context()) return;
                    if (wait && !scheduler::is_current_thread(this))
                        try_await_while([&]() { return this->is_running(); });
                }

                virtual ~task_base()
                {
                    for (auto e : exceptions)
                    {
                        try
                        {
                            try { std::rethrow_exception(e); }
                            catch (...) { std::throw_with_nested(thread_exception { nullptr }); }
                        }
                        catch (...) { parent->exceptions.push_back(std::current_exception()); }
                    }
                    exceptions.clear();
                }
            };

            template<typename> class task_impl;

            template<typename R, typename... A>
            class task_impl<R(A...)> : public task_base
            {
                template<typename> friend class task;
                using base = task_base;

            protected:
                std::function<R(A...)> function;
                std::unique_ptr<std::tuple<A...>> arguments;
                std::optional<typename std::conditional_t<std::is_void_v<R>, int, R>> result;

                virtual void call() override
                {
                    auto f = [this] { return std::apply(function, *arguments); };
                    if constexpr (std::is_void_v<R>) f();
                    else result = std::make_optional<R>(f());
                }

            public:
                // (Re-)start the task using the specified arguments.
                // May rethrow unhandled exceptions.
                template <typename... Args>
                void start(Args&&... args)
                {
                    if (this->is_running()) return;
                    arguments = std::make_unique<std::tuple<A...>>(std::forward<Args>(args)...);
                    result.reset();
                    base::start();
                }

                // Blocks until the task returns a result, or terminates.
                // Returns true when it is safe to call await() to obtain the result.
                // May rethrow unhandled exceptions!
                bool try_await()
                {
                    dpmi::throw_if_irq();
                    if (scheduler::is_current_thread(this)) return false;

                    this->try_await_while([this]() { return this->is_running(); });

                    if constexpr (!std::is_void_v<R>) if (this->state == initialized) return false;
                    return true;
                }

                // Awaits a result from the task.
                // Throws illegal_await if the task terminates without returning a result.
                // May rethrow unhandled exceptions!
                decltype(auto) await()
                {
                    if (!try_await()) throw illegal_await(this->shared_from_this());

                    this->state = initialized;
                    if constexpr (!std::is_void_v<R>) return std::move(*result);
                }

                template <typename F>
                task_impl(F&& f, std::size_t stack_bytes = config::thread_default_stack_size)
                    : base { stack_bytes }
                    , function { std::forward<F>(f) } { }
            };

            template<typename task_type>
            class task_ptr
            {
                std::shared_ptr<task_type> ptr;

            public:
                constexpr const auto get_ptr() const noexcept { return ptr; }
                constexpr auto* operator->() const { return ptr.get(); }
                constexpr auto& operator*() const { return *ptr; }
                constexpr operator bool() const { return static_cast<bool>(ptr); }

                template<typename... Args>
                constexpr task_ptr(Args&&... a) : ptr(std::allocate_shared<task_type>(*detail::pool_alloc, std::forward<Args>(a)...)) { }

                constexpr task_ptr(const task_ptr&) = default;
                constexpr task_ptr() = default;
            };
        }

        template<typename Sig> struct task : public detail::task_ptr<detail::task_impl<Sig>> { };

        template<typename R, typename... A>
        task(R(*)(A...)) -> task<R(A...)>;

        template<typename F, typename Sig = typename std::__function_guide_helper<decltype(&F::operator())>::type>
        task(F) -> task<Sig>;
    }
}
