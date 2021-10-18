/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
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
#include <jw/function.h>

namespace jw
{
    namespace thread
    {
        namespace detail
        {
            inline dpmi::locked_pool_resource<true>* scheduler_memres;

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
                starting,
                running,
                suspended,
                aborting,
                aborted,
                finished
            };

            struct thread
            {
                friend struct scheduler;

                static inline std::uint32_t id_count { 1 };

                template <typename F>
                thread(F&& func, std::size_t bytes)
                    : function { std::forward<F>(func) }
                    , stack { bytes > 0 ? new byte[bytes] : nullptr, bytes } { }
                ~thread() { if (stack.data() != nullptr) delete stack.data(); }

                thread& operator=(const thread&) = delete;
                thread(const thread&) = delete;
                thread& operator=(thread&&) = delete;
                thread(thread&&) = delete;

                const std::uint32_t id { id_count++ };
                const std::function<void()> function;
                const std::span<std::byte> stack;
                thread_context* context; // points to esp during context switch
                thread_state state { starting };

                std::pmr::deque<jw::function<void()>> invoke_list { };

                void abort() noexcept
                {
                    if (not active()) return;
                    this->state = aborting;
                }

                // Returns true if this thread is running.
                bool active() const noexcept { return state != finished and state != aborted; }

                // Get the current detail::thread_state (initialized, starting, running, suspended, terminating, finished)
                thread_state get_state() const noexcept { return state; }

                // Name of this thread, for use in exceptions and gdb.
                std::string name { "anonymous thread" };

                // Suspend this thread, if it was previously running.
                void suspend() noexcept { if (state == running) state = suspended; }

                // Resume this thread, if it was previously suspended.
                void resume() noexcept { if (state == suspended) state = running; }

                // Invoke a funcion on this thread.
                template<typename F> void invoke(F&& function) { invoke_list.emplace_back(std::forward<F>(function)); }
            };

            using thread_ptr = std::shared_ptr<thread>;
        }
    }
}
