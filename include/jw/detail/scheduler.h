/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <functional>
#include <memory>
#include <deque>
#include <map>
#include <jw/dpmi/fpu.h>
#include <jw/dpmi/irq_check.h>
#include <jw/dpmi/alloc.h>
#include <jw/function.h>
#include <jw/main.h>
#include <jw/detail/eh_globals.h>


namespace jw
{
    struct thread;
}

namespace jw::this_thread
{
    void yield();
}

namespace jw::detail
{
    struct thread;
    using thread_ptr = std::shared_ptr<thread>;

    struct [[gnu::packed]] thread_context
    {
        std::uint32_t gs;
        std::uint32_t fs;
        dpmi::cpu_flags flags;
        std::uint32_t ebx;
        std::uint32_t esi;
        std::uint32_t edi;
        std::uint32_t ebp;
        std::uintptr_t return_address;
        // eax, ecx, edx are caller-saved.
        // cs, ds, es, ss (should) never change.
        // esp is the pointer to this struct.
    };

    struct scheduler
    {
        friend int ::main(int, const char**);
        friend void ::jw::this_thread::yield();
        friend struct ::jw::thread;
        friend struct ::jw::init;

        template <typename T = std::byte>
        using allocator = monomorphic_allocator<dpmi::locked_pool_resource<true>, T>;
        using thread_id = std::uint32_t;

        static constexpr inline thread_id main_thread_id = 1;

        static bool is_current_thread(const thread* t) noexcept;
        static bool is_current_thread(thread_id) noexcept;
        static thread* current_thread() noexcept;
        static thread_id current_thread_id() noexcept;
        static thread* get_thread(thread_id) noexcept;

        template<typename F>
        static void invoke_main(F&& function);
        template<typename F>
        static void invoke_next(F&& function);

        static auto* memory_resource() noexcept { return memres; }

#       ifndef NDEBUG
        static const auto& all_threads() { return instance->threads; }
#       endif

    private:
        using map_t = std::map<thread_id, thread_ptr, std::less<thread_id>, allocator<std::pair<const thread_id, thread_ptr>>>;
        template<typename F>
        static thread_ptr create_thread(F&& func, std::size_t stack_size = config::thread_default_stack_size);
        static void start_thread(const thread_ptr&);
        static void atexit(thread*);

        [[gnu::hot]]
        static void yield();

        [[gnu::hot, gnu::noinline, gnu::noclone, gnu::naked]]
        static void context_switch(thread_context**);

        [[gnu::hot, gnu::noinline, gnu::cdecl]]
        static thread_context* switch_thread();

        [[gnu::force_align_arg_pointer, noreturn]]
        static void run_thread() noexcept;

        map_t threads { memres };
        map_t::iterator iterator;
        thread_ptr main_thread;
        bool terminating { false };

        static void setup();
        static void kill_all();
        scheduler();

        inline static constinit dpmi::locked_pool_resource<true>* memres { nullptr };
        inline static constinit scheduler* instance { nullptr };

        struct dtor { ~dtor(); } static inline destructor;
    };

    struct thread
    {
        friend struct scheduler;
        friend struct dpmi::fpu_context;

        enum thread_state
        {
            starting,
            running,
            finishing,
            finished
        };

        const scheduler::thread_id id { id_count++ };

        bool active() const noexcept { return state != finished; }
        void suspend() noexcept { suspended = true; }
        void resume() noexcept { suspended = false; }
        void abort() noexcept { aborted = true; }
        auto get_state() const noexcept { return state; }
        bool is_aborted() const noexcept { return aborted; }
        bool is_suspended() const noexcept { return suspended; }

        template<typename F> void invoke(F&& function) { invoke_list.emplace_back(std::forward<F>(function)); }
        template<typename F> void atexit(F&& function) { atexit_list.emplace_back(std::forward<F>(function)); }

#       ifdef NDEBUG
        void set_name(...) const noexcept { }
        std::string_view get_name() const noexcept { return { }; }
#       else
        template<typename T>
        void set_name(T&& string) { name = std::forward<T>(string); }
        std::string_view get_name() const noexcept { return name; }
        thread_context* get_context() noexcept { return context; }
#       endif

        ~thread()
        {
            if (function.data() == nullptr) return;
            destroy(function.data());
            memres.deallocate(function.data(), function.size() + stack.size());
        }

    private:
        thread() = default;

        template <typename F, typename function_t = std::remove_cvref_t<F>>
        thread(F&& func, std::size_t stack_bytes)
            : thread { allocate<function_t>(stack_bytes), std::forward<F>(func) } { }

        template <typename F, typename function_t = std::remove_cvref_t<F>>
        thread(std::span<std::byte> span, F&& func)
            : function { span.first(sizeof(function_t)) }
            , call { do_call<function_t> }
            , destroy { do_destroy<function_t> }
            , stack { span.last(span.size() - sizeof(function_t)) }
        {
            new(function.data()) function_t { std::forward<F>(func) };
        }

        thread& operator=(const thread&) = delete;
        thread(const thread&) = delete;
        thread& operator=(thread&&) = delete;
        thread(thread&&) = delete;

        void operator()() { call(function.data()); }

        template<typename F>
        static auto allocate(std::size_t stack)
        {
            const auto n = stack + sizeof(F);
            const auto a = std::max(alignof(F), 4ul);
            auto* const p = memres.allocate(n, a);
            return std::span<std::byte> { static_cast<std::byte*>(p), n };
        }

        template<typename F>
        static void do_call(void* f) { (*static_cast<F*>(f))(); }
        template<typename F>
        static void do_destroy(void* f) { static_cast<F*>(f)->~F(); }

        static inline scheduler::thread_id id_count { scheduler::main_thread_id };
        static inline pool_resource memres { 4 * config::thread_default_stack_size };

        const std::span<std::byte> function { };
        void (*call)(void*);
        void (*destroy)(void*);
        const std::span<std::byte> stack;
        thread_context* context; // points to esp during context switch
        jw_cxa_eh_globals eh_globals { };
        dpmi::detail::fpu_state* restore { nullptr };
        thread_state state { starting };
        bool suspended { false };
        bool aborted { false };

        std::deque<jw::function<void(), 4>, scheduler::allocator<jw::function<void(), 4>>> invoke_list { scheduler::memory_resource() };
        std::deque<jw::function<void(), 4>> atexit_list { };

#       ifndef NDEBUG
        std::pmr::string name { "anonymous thread", scheduler::memory_resource() };
#       endif
    };

    struct abort_thread
    {
        ~abort_thread() noexcept(false) { if (not defused) throw terminate_exception { }; }
        virtual const char* what() const noexcept { return "Thread aborted."; }
    private:
        friend struct scheduler;
        void defuse() const noexcept { defused = true; }
        mutable bool defused { false };
    };

    inline bool scheduler::is_current_thread(const thread* t) noexcept { return current_thread() == t; }
    inline bool scheduler::is_current_thread(thread_id id) noexcept { return current_thread_id() == id; }
    inline thread* scheduler::current_thread() noexcept { return instance->iterator->second.get(); }
    inline scheduler::thread_id scheduler::current_thread_id() noexcept { return instance->iterator->first; }

    inline thread* scheduler::get_thread(thread_id id) noexcept
    {
        auto* const i = instance;
        auto it = i->threads.find(id);
        if (it == i->threads.end()) return nullptr;
        return it->second.get();
    }

    template<typename F>
    inline void scheduler::invoke_main(F&& function)
    {
        if (current_thread_id() == main_thread_id and not dpmi::in_irq_context()) std::forward<F>(function)();
        else instance->main_thread->invoke(std::forward<F>(function));
    }

    template<typename F>
    inline void scheduler::invoke_next(F&& function)
    {
        auto* const i = instance;
        if (not i->threads.empty())
        {
            auto next = i->iterator;
            if (++next == i->threads.end()) next = i->threads.begin();
            next->second->invoke(std::forward<F>(function));
        }
        else if (dpmi::in_irq_context()) current_thread()->invoke(std::forward<F>(function));
        else std::forward<F>(function)();
    }

    template<typename F>
    inline thread_ptr scheduler::create_thread(F&& func, std::size_t stack_size)
    {
        allocator<thread> alloc { memory_resource() };
        allocator_delete<allocator<thread>> deleter { alloc };
        auto* p = alloc.allocate(1);
        try { p = new (p) thread { std::forward<F>(func), stack_size }; }
        catch (...) { alloc.deallocate(p, 1); throw; }
        return { p, deleter, alloc };
    }
}
