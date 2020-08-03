/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <atomic>
#include <variant>
#include <jw/thread/thread.h>
#include <jw/thread/detail/mutex.h>
#include <jw/dpmi/irq.h>
#include <jw/dpmi/detail/interrupt_id.h>

namespace jw
{
    namespace thread
    {
        struct deadlock : public std::runtime_error { deadlock() : runtime_error("deadlock") { } };

        class mutex
        {
            std::atomic_flag locked { false };
        public:
            constexpr mutex() noexcept = default;
            mutex(mutex&&) = delete;
            mutex(const mutex&) = delete;

            void lock()
            {
                if (dpmi::in_irq_context())
                {
                    if (try_lock()) return;
                    else throw deadlock { };
                }
                yield_while([this]() { return !try_lock(); });
            }
            void unlock() noexcept
            {
                locked.clear();
            }
            bool try_lock() noexcept 
            {
                return !locked.test_and_set();
            }
        };

        class recursive_mutex
        {
            using thread_ptr = std::weak_ptr<const detail::thread>;
            using irq_ptr = std::weak_ptr<const dpmi::detail::interrupt_id::id_t>;
            std::variant<thread_ptr, irq_ptr, std::nullptr_t> owner { nullptr };
            std::atomic<std::uint32_t> lock_count { 0 };

            struct is_owner
            {
                bool operator()(const thread_ptr& p) const noexcept { return detail::scheduler::is_current_thread(p.lock().get()); }
                bool operator()(const irq_ptr& p) const noexcept { return dpmi::detail::interrupt_id::is_current_interrupt(p.lock().get()); }
                bool operator()(const std::nullptr_t&) const noexcept { return false; }
            };

            struct has_owner
            {
                bool operator()(const thread_ptr& p) const noexcept { return not p.expired(); }
                bool operator()(const irq_ptr& p) const noexcept { return not p.expired(); }
                bool operator()(const std::nullptr_t&) const noexcept { return false; }
            };

        public:
            constexpr recursive_mutex() noexcept = default;
            recursive_mutex(recursive_mutex&&) = delete;
            recursive_mutex(const recursive_mutex&) = delete;

            void lock()
            {
                if (dpmi::in_irq_context())
                {
                    if (try_lock()) return;
                    else throw deadlock { };
                }
                yield_while([this]() { return !try_lock(); });
            }

            void unlock() noexcept
            {
                if (std::visit(is_owner { }, owner)) --lock_count;
                if (lock_count == 0) owner = nullptr;
            }

            bool try_lock() noexcept
            {
                if (owner.valueless_by_exception() or not std::visit(has_owner { }, owner))
                {
                    if (dpmi::in_irq_context()) owner = dpmi::detail::interrupt_id::get_current_interrupt();
                    else owner = detail::scheduler::get_current_thread();
                    lock_count = 1;
                    return true;
                }
                else if (std::visit(is_owner { }, owner))
                {
                    ++lock_count;
                    return true;
                }
                return false;
            }
        };

        using timed_mutex = detail::timed_mutex_adapter<mutex>;
        using recursive_timed_mutex = detail::timed_mutex_adapter<recursive_mutex>;
    }
}
