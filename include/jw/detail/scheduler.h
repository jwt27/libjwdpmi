/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2016 - 2024 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <jw/dpmi/irq_check.h>
#include <jw/dpmi/alloc.h>
#include <jw/function.h>
#include <jw/main.h>
#include <jw/detail/eh_globals.h>
#include <jw/debug.h>
#include <functional>
#include <memory>
#include <deque>
#include <set>
#include <optional>
#include <unwind.h>

namespace jw
{
    struct thread;
}

namespace jw::detail
{
    using thread_id = std::uint32_t;

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

    template <typename T = std::byte>
    using thread_allocator = monomorphic_allocator<dpmi::locked_pool_resource, T>;

    struct thread
    {
        friend struct scheduler;

        static constexpr inline thread_id main_thread_id = 1;

        enum thread_state : std::uint8_t
        {
            starting,
            running,
            finishing,
            finished
        };

        const thread_id id { id_count++ };

        bool active() const noexcept { return state != finished; }
        void suspend() noexcept { suspended = true; }
        void resume() noexcept { suspended = false; }
        void cancel() noexcept { canceled = true; }
        void detach() noexcept { detached = true; }
        auto get_state() const noexcept { return state; }
        bool is_canceled() const noexcept { return canceled; }
        bool is_suspended() const noexcept { return suspended; }
        bool is_unwinding() const noexcept { return unwinding; }

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

        thread();

        template <typename F, typename function_t = std::remove_cvref_t<F>>
        thread(F&& func, std::size_t stack_bytes)
            : thread { allocate<function_t>(stack_bytes), std::forward<F>(func) } { }

    private:
        template <typename F, typename function_t = std::remove_cvref_t<F>>
        thread(std::span<std::byte> span, F&& func);

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

        static inline thread_id id_count { main_thread_id };
        static inline pool_resource memres { 4 * config::thread_default_stack_size };

        const std::span<std::byte> function { };
        void (*call)(void*);
        void (*destroy)(void*);
        const std::span<std::byte> stack;
        thread_context* context; // points to esp during context switch
        abi::__cxa_eh_globals eh_globals { };
        ::_Unwind_Exception unwind_exception;
        int errno { 0 };
        thread_state state { starting };
        bool suspended { false };
        bool canceled { false };
        bool detached { false };
        bool unwinding { false };

        std::deque<jw::function<void(), 4>, thread_allocator<jw::function<void(), 4>>> invoke_list;
        std::deque<jw::function<void(), 4>> atexit_list { };

#       ifndef NDEBUG
        std::pmr::string name;
#       endif
    };

    constexpr std::strong_ordering operator<=>(const thread& a, const thread& b) noexcept { return a.id <=> b.id; }
    constexpr std::strong_ordering operator<=>(const thread& a, thread_id b) noexcept { return a.id <=> b; }
    constexpr std::strong_ordering operator<=>(thread_id a, const thread& b) noexcept { return a <=> b.id; }

    struct scheduler
    {
        friend int ::main(int, const char**);
        friend struct ::jw::thread;
        friend struct ::jw::init;

        [[gnu::hot]] static void yield();
        [[gnu::hot]] static void safe_yield();

        static bool is_current_thread(const thread* t) noexcept;
        static bool is_current_thread(thread_id) noexcept;
        static thread* current_thread() noexcept;
        static thread_id current_thread_id() noexcept;
        static thread* get_thread(thread_id) noexcept;

        [[noreturn]] static void forced_unwind();
        static void catch_forced_unwind() noexcept;

        template<typename F>
        static void invoke_main(F&& function);
        template<typename F>
        static void invoke_next(F&& function);

        static auto* memory_resource() noexcept { return &*memres; }

#       ifndef NDEBUG
        static const auto& all_threads() { return *threads; }
#       endif

    private:
        using set_type = std::set<thread, std::less<void>, thread_allocator<thread>>;
        template<typename F>
        static thread* create_thread(F&& func, std::size_t stack_size);
        static void atexit(thread*) noexcept;

        template<bool>
        static void do_yield();

        [[gnu::hot, gnu::noinline, gnu::noclone, gnu::naked, gnu::regparm(1)]]
        static void context_switch(thread_context**);

        [[gnu::hot, gnu::noinline, gnu::cdecl]]
        static thread_context* switch_thread();

        [[gnu::force_align_arg_pointer, noreturn]]
        static void run_thread() noexcept;

        static void setup();
        static void kill_all();

        inline static constinit std::optional<dpmi::locked_pool_resource> memres { std::nullopt };
        inline static constinit std::optional<set_type> threads { std::nullopt };
        inline static constinit std::optional<set_type::iterator> iterator { std::nullopt };
    };

    inline bool scheduler::is_current_thread(const thread* t) noexcept { return current_thread() == t; }
    inline bool scheduler::is_current_thread(thread_id id) noexcept { return current_thread_id() == id; }
    inline thread* scheduler::current_thread() noexcept { return const_cast<thread*>(&**iterator); }
    inline thread_id scheduler::current_thread_id() noexcept { return (*iterator)->id; }

    inline thread* scheduler::get_thread(thread_id id) noexcept
    {
        auto it = threads->find(id);
        if (it == threads->end()) return nullptr;
        return const_cast<thread*>(&*it);
    }

    template<typename F>
    inline void scheduler::invoke_main(F&& function)
    {
        if (current_thread_id() == thread::main_thread_id and not dpmi::in_irq_context()) std::forward<F>(function)();
        else const_cast<thread&>(*threads->begin()).invoke(std::forward<F>(function));
    }

    template<typename F>
    inline void scheduler::invoke_next(F&& function)
    {
        if (not threads->empty())
        {
            auto next = *iterator;
            if (++next == threads->end()) next = threads->begin();
            const_cast<thread&>(*next).invoke(std::forward<F>(function));
        }
        else if (dpmi::in_irq_context()) current_thread()->invoke(std::forward<F>(function));
        else std::forward<F>(function)();
    }

    template<typename F>
    inline thread* scheduler::create_thread(F&& func, std::size_t stack_size)
    {
        debug::trap_mask dont_trace_here { };
        dpmi::interrupt_mask no_interrupts_please { };
        auto i = threads->emplace_hint(threads->end(), std::forward<F>(func), stack_size);
        return const_cast<thread*>(&*i);
    }

    inline thread::thread()
        : invoke_list { scheduler::memory_resource() }
#       ifndef NDEBUG
        , name { scheduler::memory_resource() }
#       endif
    { }

    template <typename F, typename function_t>
    inline thread::thread(std::span<std::byte> span, F&& func)
        : function { span.first(sizeof(function_t)) }
        , call { do_call<function_t> }
        , destroy { do_destroy<function_t> }
        , stack { span.last(span.size() - sizeof(function_t)) }
        , invoke_list { scheduler::memory_resource() }
#       ifndef NDEBUG
        , name { "anonymous thread", scheduler::memory_resource() }
#       endif
    {
        new(function.data()) function_t { std::forward<F>(func) };
    }
}
