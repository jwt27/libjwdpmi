/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <functional>
#include <memory>
#include <deque>
#include <iostream>
#include <jw/dpmi/alloc.h>
#include <jw/common.h>

namespace jw
{
    namespace thread
    {
        namespace detail
        {
            inline dpmi::locked_pool_allocator<>* pool_alloc;

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

            enum thread_state
            {
                initialized,
                starting,
                running,
                suspended,
                terminating,
                finished
            };

            // Base class for all threads.
            class thread
            {
                friend class scheduler;
                friend class task_base;
                friend class thread_details;

                static inline std::uint32_t id_count { 1 };

                thread_context* context; // points to esp during context switch
                std::unique_ptr<byte[]> stack;
                std::size_t stack_size;
                std::deque<std::exception_ptr> exceptions { };
                const std::uint32_t id_num { id_count++ };
                std::deque<std::function<void()>, dpmi::locked_pool_allocator<true, std::function<void()>>> invoke_list { *pool_alloc };

            protected:
                thread_state state { initialized };
                std::shared_ptr<thread> parent;
                std::shared_ptr<thread> awaiting;

                virtual void call() { throw std::bad_function_call(); };

                thread& operator=(const thread&) = delete;
                thread(const thread&) = delete;

                thread(std::size_t bytes) : stack(new byte[bytes]), stack_size(bytes) { }

            public:
                virtual void abort(bool = true)
                {
                    if (!this->is_running()) return;
                    this->state = terminating;
                }

                // Returns true if this thread is running.
                bool is_running() const noexcept { return (state != initialized && state != finished); }

                // Returns the number of pending exceptions on this thread.
                std::size_t pending_exceptions() const noexcept { return __builtin_expect(exceptions.size(), 0); }

                // Get the unique ID number for this thread.
                std::uint32_t id() const noexcept { return id_num; }

                // Get the current detail::thread_state (initialized, starting, running, suspended, terminating, finished)
                thread_state get_state() const noexcept { return state; }

                // Name of this thread, for use in exceptions and gdb.
                std::string name { "anonymous thread" };

                // Allow orphaning (losing the pointer to) this thread while it is still active.
                bool allow_orphan { false };

                // Suspend this thread, if it was previously running.
                void suspend() noexcept { if (state == running) state = suspended; }

                // Resume this thread, if it was previously suspended.
                void resume() noexcept { if (state == suspended) state = running; }

                // Invoke a funcion on this thread.
                template<typename F> void invoke(F&& function) { invoke_list.emplace_back(std::forward<F>(function)); }

                virtual ~thread()
                {
                    if (pending_exceptions() > 0)
                    {
                        std::cerr << "Destructed thread had pending exceptions!\n";
                        std::cerr << "This should never happen. Terminating.\n";
                        std::terminate();
                    }
                }
            };

            // Proxy class to access implementation details of threads. Used by gdb interface.
            // This exists so these functions aren't exposed through task/coroutine objects.
            struct thread_details
            {
            #ifndef NDEBUG
                static const thread_context* get_context(auto t) noexcept { return t->context; }
            #endif
            };

            using thread_ptr = std::shared_ptr<thread>;
        }
    }
}
