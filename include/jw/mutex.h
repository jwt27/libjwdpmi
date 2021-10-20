/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <atomic>
#include <variant>
#include <jw/thread.h>
#include <jw/detail/mutex.h>
#include <jw/dpmi/irq.h>
#include <jw/dpmi/detail/interrupt_id.h>
#include <jw/variant.h>

namespace jw
{
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
            this_thread::yield_while([this]() { return not try_lock(); });
        }
        void unlock() noexcept
        {
            locked.clear();
        }
        bool try_lock() noexcept
        {
            return not locked.test_and_set();
        }
    };

    class recursive_mutex
    {
        using thread_id = detail::scheduler::thread_id;
        using irq_id = std::uint64_t;
        std::variant<std::nullptr_t, thread_id, irq_id> owner { nullptr };
        std::atomic<std::uint32_t> lock_count { 0 };

        struct is_owner
        {
            bool operator()(const thread_id& id) const noexcept { return detail::scheduler::current_thread_id() == id; }
            bool operator()(const irq_id& id) const noexcept { return dpmi::detail::interrupt_id::get_id() == id; }
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
            this_thread::yield_while([this]() { return not try_lock(); });
        }

        void unlock() noexcept
        {
            if (--lock_count == 0) owner = nullptr;
        }

        bool try_lock() noexcept
        {
            if (owner.index() == 0)
            {
                if (dpmi::in_irq_context()) owner = dpmi::detail::interrupt_id::get_id();
                else owner = detail::scheduler::current_thread_id();
                lock_count = 1;
                return true;
            }
            else if (visit(is_owner { }, owner))
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
