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
#include <functional>
#include <memory>
#include <deque>
#include <iostream>
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
                    if (pending_exceptions() > 0)
                    {
                        std::cerr << "Destructed thread had pending exceptions!\n";
                        std::cerr << "This should never happen. Terminating.\n";
                        std::terminate();
                    }
                }
            };

            using thread_ptr = std::shared_ptr<thread>;
        }
    }
}
