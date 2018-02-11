/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */

#include <atomic>
#include <jw/thread/thread.h>

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
}
