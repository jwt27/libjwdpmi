/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <iostream>
#include <functional>
#include <memory>
#include <deque>
#include <jw/dpmi/irq_check.h>
#include <jw/dpmi/alloc.h>
#include <jw/function.h>
#include <jw/main.h>


namespace jw::thread
{
    void yield();
    struct thread;
}

namespace jw::thread::detail
{
    struct thread;
    using thread_ptr = std::shared_ptr<thread>;

    struct [[gnu::packed]] thread_context
    {
        std::uint32_t gs;
        std::uint32_t fs;
        std::uint32_t es;
        std::uint32_t ebx;
        std::uint32_t esi;
        std::uint32_t edi;
        std::uint32_t ebp;
        std::uintptr_t return_address;
        // eax, ecx, edx are caller-saved.
        // cs, ds, ss (should) never change.
        // esp is the pointer to this struct.
    };

    struct scheduler
    {
        friend int ::main(int, const char**);
        friend void ::jw::thread::yield();
        friend struct ::jw::thread::thread;
        friend struct ::jw::init;

        template <typename T = std::byte>
        using allocator = monomorphic_allocator<dpmi::locked_pool_resource<true>, T>;

        static bool is_current_thread(const thread* t) noexcept;
        static std::weak_ptr<thread> get_current_thread() noexcept;
        static auto get_current_thread_id() noexcept;
        static const auto& get_threads();

        template<typename F>
        static void invoke_main(F&& function);
        template<typename F>
        static void invoke_next(F&& function);

        static auto* memory_resource() noexcept { return memres; }

    private:
        template<typename F>
        static thread_ptr create_thread(F&& func, std::size_t stack_size = config::thread_default_stack_size);
        static void start_thread(const thread_ptr&);
        static void yield();
        static void check_exception();

        [[gnu::noinline, gnu::noclone, gnu::naked]]
        static void context_switch(thread_context**);

        [[gnu::noinline, gnu::cdecl]]
        static thread_context* switch_thread();

        [[gnu::force_align_arg_pointer, noreturn]]
        static void run_thread() noexcept;

        std::deque<thread_ptr, allocator<thread_ptr>> threads { memres };
        thread_ptr current_thread;
        thread_ptr main_thread;
        bool terminating { false };

        static void setup();
        static void kill_all();
        scheduler();

        inline static constinit dpmi::locked_pool_resource<true>* memres { nullptr };
        inline static constinit scheduler* instance { nullptr };

        struct dtor { ~dtor(); } static inline destructor;
    };

    enum thread_state
    {
        starting,
        running,
        suspended,
        aborting,
        aborted,
        finished
    };

    struct thread
    {
        static inline std::uint32_t id_count { 1 };
        static inline pool_resource stack_resource { 4 * config::thread_default_stack_size };

        thread() = default;

        template <typename F>
        thread(F&& func, std::size_t bytes)
            : function { std::forward<F>(func) }
            , stack { static_cast<std::byte*>(stack_resource.allocate(bytes)), bytes } { }
        ~thread() { if (stack.data() != nullptr) stack_resource.deallocate(stack.data(), stack.size_bytes()); }

        thread& operator=(const thread&) = delete;
        thread(const thread&) = delete;
        thread& operator=(thread&&) = delete;
        thread(thread&&) = delete;

        const std::uint32_t id { id_count++ };
        const std::function<void()> function;
        const std::span<std::byte> stack;
        thread_context* context; // points to esp during context switch
        thread_state state { starting };

        std::deque<jw::function<void()>, scheduler::allocator<jw::function<void()>>> invoke_list { scheduler::memory_resource() };

        void abort() noexcept
        {
            if (not active()) return;
            this->state = aborting;
        }

        bool active() const noexcept { return state != finished and state != aborted; }

        void suspend() noexcept { if (state == running) state = suspended; }
        void resume() noexcept { if (state == suspended) state = running; }

        template<typename F> void invoke(F&& function) { invoke_list.emplace_back(std::forward<F>(function)); }

#       ifdef NDEBUG
        void set_name(...) const noexcept { }
#       else
        template<typename T>
        void set_name(T&& string) { name = std::forward<T>(string); }
        std::pmr::string name { "anonymous thread", scheduler::memory_resource() };
#       endif
    };

    inline bool scheduler::is_current_thread(const thread* t) noexcept { return instance->current_thread.get() == t; }
    inline std::weak_ptr<thread> scheduler::get_current_thread() noexcept { return instance->current_thread; }
    inline auto scheduler::get_current_thread_id() noexcept { return instance->current_thread->id; }
    inline const auto& scheduler::get_threads() { return instance->threads; }

    template<typename F>
    inline void scheduler::invoke_main(F&& function)
    {
        if (is_current_thread(instance->main_thread.get()) and not dpmi::in_irq_context()) std::forward<F>(function)();
        else instance->main_thread->invoke(std::forward<F>(function));
    }

    template<typename F>
    inline void scheduler::invoke_next(F&& function)
    {
        if (not instance->threads.empty()) instance->threads.front()->invoke(std::forward<F>(function));
        else if (dpmi::in_irq_context()) instance->current_thread->invoke(std::forward<F>(function));
        else std::forward<F>(function)();
    }

    template<typename F>
    inline thread_ptr scheduler::create_thread(F&& func, std::size_t stack_size)
    {
        scheduler::allocator<thread> alloc { scheduler::memory_resource() };
        return std::allocate_shared<thread>(alloc, std::forward<F>(func), stack_size);
    }
}
