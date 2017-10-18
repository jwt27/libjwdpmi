/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/thread/detail/thread.h>
#include <jw/thread/detail/scheduler.h>
#include <jw/thread/thread.h>
#include <../jwdpmi_config.h>

namespace jw
{
    namespace thread
    {
        namespace detail
        {
            template<std::size_t stack_bytes>
            class task_base : public detail::thread, public std::enable_shared_from_this<task_base<stack_bytes>>
            {
                alignas(0x10) std::array<byte, stack_bytes> stack;

            protected:
                constexpr task_base() : thread(stack_bytes, stack.data()) { }

                constexpr void start()
                {
                    if (this->is_running()) return;

                    this->state = starting;
                    this->parent = scheduler::current_thread;
                    if (dpmi::in_irq_context()) this->parent = scheduler::main_thread;
                    scheduler::thread_switch(this->shared_from_this());
                }

                template<typename F>
                void try_await_while(F f)
                {
                    scheduler::current_thread->awaiting = this->shared_from_this();
                    try
                    {
                        do { yield(); } while (f());
                    }
                    catch (...)
                    {
                        scheduler::current_thread->awaiting.reset();
                        throw;
                    }
                    scheduler::current_thread->awaiting.reset();
                }

            public:
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

            template<typename sig, std::size_t stack_bytes>
            class task_impl;

            template<typename R, typename... A, std::size_t stack_bytes>
            class task_impl<R(A...), stack_bytes> : public task_base<stack_bytes>
            {
                template<typename, std::size_t> friend class task;
                using base = task_base<stack_bytes>;

            protected:
                std::function<R(A...)> function;
                std::unique_ptr<std::tuple<A...>> arguments;
                std::optional<typename std::conditional<std::is_void<R>::value, int, R>::type> result; // std::experimental::optional ?

                virtual void call() override
                {
                    auto f = [this] { return std::apply(function, *arguments); };
                    if constexpr (std::is_void_v<R>) f();
                    else result = std::make_optional<R>(f());
                }

            public:
                // Start the task using the specified arguments.
                template <typename... Args>
                constexpr void start(Args&&... args)
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
                auto await()
                {
                    if (!try_await()) throw illegal_await(this->shared_from_this());

                    this->state = initialized;
                    if constexpr (!std::is_void_v<R>) return std::move(*result);
                }

                template <typename F>
                task_impl(F&& f) : function(std::forward<F>(f)) { }   // TODO: allocator support!
            };
        }

        template<typename, std::size_t = config::thread_default_stack_size> class task;

        template<typename R, typename... A, std::size_t stack_bytes>
        class task<R(A...), stack_bytes>
        {
            using task_type = detail::task_impl<R(A...), stack_bytes>;
            std::shared_ptr<task_type> ptr;

        public:
            constexpr const auto get_ptr() const noexcept { return ptr; }
            constexpr auto* operator->() const { return ptr.get(); }
            constexpr auto& operator*() const { return *ptr; }
            constexpr operator bool() const { return ptr.operator bool(); }

            template<typename F>
            constexpr task(F&& f) : ptr(std::make_shared<task_type>(std::forward<F>(f))) { }
            
            template<typename F, typename Alloc>
            constexpr task(std::allocator_arg_t, Alloc&& a, F&& f) : ptr(std::allocate_shared<task_type>(std::forward<Alloc>(a), std::forward<F>(f))) { }

            constexpr task(const task&) = default;
            constexpr task() = default;
        };

        template<typename R, typename... A, std::size_t stack_bytes = config::thread_default_stack_size>
        task(R(*)(A...)) -> task<R(A...), stack_bytes>;

        template<typename F, typename Sig = typename std::__function_guide_helper<decltype(&F::operator())>::type, std::size_t stack_bytes = config::thread_default_stack_size>
        task(F) -> task<Sig, stack_bytes>;

        template<typename Sig, std::size_t stack_bytes, typename... T>
        auto allocate_task(T&&... args) { return task<Sig, stack_bytes> { std::allocator_arg, std::forward<T>(args)... }; }
    }
}
