/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <atomic>
#include <jw/thread/thread.h>

namespace jw
{
    namespace thread
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
                dpmi::throw_if_irq(); 
                yield_while([&]() { return !try_lock(); }); 
            }
            void unlock() noexcept { locked.clear(); }
            bool try_lock() noexcept { return !locked.test_and_set(); }
        };

        class recursive_mutex
        {
            std::atomic<std::uint32_t> lock_count { 0 };
            std::weak_ptr<const detail::thread> owner;

        public:
            constexpr recursive_mutex() noexcept = default;
            recursive_mutex(recursive_mutex&&) = delete;
            recursive_mutex(const recursive_mutex&) = delete;

            void lock() 
            { 
                dpmi::throw_if_irq(); 
                yield_while([&]() { return !try_lock(); }); 
            }
            void unlock() noexcept
            {
                if (detail::scheduler::is_current_thread(owner.lock().get())) --lock_count;
                if (lock_count == 0) owner.reset();
            }
            bool try_lock() noexcept
            {
                if (dpmi::in_irq_context()) return false;
                if (!owner.lock())
                {
                    owner = detail::scheduler::get_current_thread();
                    lock_count = 1;
                    return true;
                }
                else if (detail::scheduler::is_current_thread(owner.lock().get()))
                {
                    ++lock_count;
                    return true;
                }
                return false;
            }
        };
    }
}
