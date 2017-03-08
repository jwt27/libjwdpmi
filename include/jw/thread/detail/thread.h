#pragma once
#include <functional>
#include <memory>
#include <deque>
#include <jw/typedef.h>

namespace jw
{
    namespace thread
    {
        namespace detail
        {
            struct[[gnu::packed]] thread_context
            {
                std::uint32_t gs;
                std::uint32_t fs;
                std::uint32_t es;
                std::uint32_t ebx;
                std::uint32_t esi;
                std::uint32_t edi;
                std::uint32_t ebp;
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

            class thread
            {
                friend class scheduler;
                template<std::size_t> friend class task_base;

                thread_context* context; // points to esp during context switch
                const std::size_t stack_size;
                byte* const stack_ptr;
                std::deque<std::exception_ptr> exceptions { };

            protected:
                thread_state state { initialized };
                std::shared_ptr<thread> parent;
                std::shared_ptr<thread> awaiting;

                virtual void call() { throw std::bad_function_call(); };

                auto& operator=(const thread&) = delete;
                thread(const thread&) = delete;

                thread(std::size_t bytes, byte* ptr) : stack_size(bytes), stack_ptr(ptr) { }

            public:
                bool is_running() const noexcept { return (state != initialized && state != finished); }
                bool allow_orphan { false };
                auto pending_exceptions() const noexcept { return exceptions.size(); }
                
                virtual ~thread()
                {
                    if (!exceptions.empty()) std::copy(exceptions.begin(), exceptions.end(), std::back_inserter(parent->exceptions));
                }
            };

            using thread_ptr = std::shared_ptr<thread>;
        }
    }
}
