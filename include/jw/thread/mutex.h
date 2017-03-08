#pragma once
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <jw/thread/thread.h>

// TODO: make these IRQ-safe

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
            bool try_lock() noexcept 
            { 
                if (dpmi::in_irq_context()) return false;
                return !locked.test_and_set(); 
            }
        };

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
                if (dpmi::in_irq_context()) return false;
                if (locked.test_and_set()) return false;
                if (shared_count == 0) return true;
                unlock();
                return false;
            }

            void lock_shared() { dpmi::throw_if_irq(); yield_while([&]() { return !try_lock_shared(); }); }
            void unlock_shared() noexcept { --shared_count; }
            bool try_lock_shared() noexcept
            {
                if (dpmi::in_irq_context()) return false;
                if (locked.test_and_set()) return false;
                ++shared_count;
                unlock();
                return true;
            }
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

#ifndef _GLIBCXX_HAS_GTHREADS
namespace std
{
    using mutex = jw::thread::mutex;
    using shared_mutex = jw::thread::shared_mutex;
    using recursive_mutex = jw::thread::recursive_mutex;
}
#endif
