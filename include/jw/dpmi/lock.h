// Provides easy means to lock classes and other data in memory. This prevents 
// things from being swapped out, thus avoiding page faults.

#pragma once
#include <jw/dpmi/dpmi.h>

namespace jw
{
    namespace dpmi
    {
        class memory_lock
        {
        public:
            memory_lock(const memory_lock& c) = delete;
            memory_lock(memory_lock&& m) noexcept : mem(m.mem) { m.locked = false; }

            memory_lock& operator=(const memory_lock& c) = delete;
            memory_lock& operator=(memory_lock&&) noexcept = default;

            virtual ~memory_lock()
            {
                try { unlock(); }
                catch (const std::exception& e)
                {
                    std::cerr << "Caught exception in memory_lock destructor! \n"
                        << "locked region: " << std::hex << mem.get_address() << " - " << (mem.get_address() + mem.get_size()) << "\n"
                        << "error: " << e.what() << std::endl;
                }
                catch (...) { std::cerr << "Caught exception in memory_lock destructor!" << std::endl; }
            }

        protected:
            template<typename T>
            memory_lock(selector segment, const T* ptr, std::size_t size_bytes) : mem(segment, ptr, size_bytes) { lock(); }

            void lock()
            {
                if (locked) return;
                mem.lock_memory();
                locked = true;
            }

            void unlock()
            {
                if (!locked) return;
                mem.unlock_memory();
                locked = false;
            }

            memory_descriptor mem;
            bool locked { false };
        };

        // Locks the memory occupied by a function (in Code Segment)
        // Finding the size of a function requires ugly hacks. It's easier to rely on _CRT0_FLAG_LOCK_MEMORY.
        struct[[deprecated]] code_lock final : public memory_lock
        {
            template<typename T>
            code_lock(const T* addr, std::size_t size_bytes) : memory_lock(get_cs(), addr, size_bytes) { }
        };

        // Locks the memory occupied by one or more objects (in Data Segment)
        struct data_lock final : public memory_lock
        {
            template<typename T>
            data_lock(const T* addr, std::size_t num_elements = 1) : memory_lock(get_ds(), addr, num_elements * sizeof(T)) { }
            data_lock(const void* addr, std::size_t size_bytes) : memory_lock(get_ds(), addr, size_bytes) { }
        };

        // Locks the memory occupied by a class (in Data Segment) that derives from this.
        template <typename T>
        struct class_lock : public memory_lock
        {
            class_lock() : memory_lock(get_ds(), this, sizeof(T)) { }
            class_lock(class_lock&&) : class_lock() { }
            class_lock(const class_lock&) : class_lock() { }
        };
    }
}
