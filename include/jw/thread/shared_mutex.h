/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */

#include <atomic>
#include <jw/thread/thread.h>
#include <jw/thread/detail/mutex.h>

namespace jw::thread
{
    class shared_mutex
    {
        std::atomic_flag locked { false };
        std::atomic<std::uint32_t> shared_count { 0 };

    public:
        constexpr shared_mutex() noexcept = default;
        shared_mutex(shared_mutex&&) = delete;
        shared_mutex(const shared_mutex&) = delete;

        void lock()
        {
            dpmi::throw_if_irq();
            yield_while([&]() { return !try_lock(); });
        }
        void unlock() noexcept { locked.clear(); }
        bool try_lock() noexcept
        {
            if (locked.test_and_set()) return false;
            if (shared_count == 0) return true;
            unlock();
            return false;
        }

        void lock_shared()
        {
            dpmi::throw_if_irq();
            yield_while([&]() { return !try_lock_shared(); });
        }
        void unlock_shared() noexcept { --shared_count; }
        bool try_lock_shared() noexcept
        {
            if (locked.test_and_set()) return false;
            ++shared_count;
            unlock();
            return true;
        }
    };

    struct shared_timed_mutex : public detail::timed_mutex_adapter<shared_mutex>
    {
        template <class Rep, class Period>
        bool try_lock_shared_for(const std::chrono::duration<Rep, Period>& rel_time)
        {
            return not yield_while_for([this] { return not this->try_lock_shared(); }, rel_time);
        }

        template <class Clock, class Duration>
        bool try_lock_shared_until(const std::chrono::time_point<Clock, Duration>& abs_time)
        {
            return not yield_while_until([this] { return not this->try_lock_shared(); }, abs_time);
        }
    };
}
