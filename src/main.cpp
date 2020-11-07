/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <cstring>
#include <string_view>
#include <vector>
#include <crt0.h>
#include <csignal>
#include <jw/alloc.h>
#include <jw/debug/debug.h>
#include <jw/dpmi/cpu_exception.h>
#include <jw/debug/detail/signals.h>
#include <jw/io/rs232.h>
#include <jw/io/ps2_interface.h>
#include <jw/dpmi/ring0.h>
#include <jw/video/ansi.h>
#include <cxxabi.h>
#include <unwind.h>
#include <../jwdpmi_config.h>

using namespace jw;

int _crt0_startup_flags = 0
| config::user_crt0_startup_flags
#ifndef NDEBUG
//| _CRT0_FLAG_NULLOK
#endif
| _CRT0_FLAG_NMI_SIGNAL
| _CRT0_DISABLE_SBRK_ADDRESS_WRAP
| _CRT0_FLAG_NONMOVE_SBRK
| _CRT0_FLAG_LOCK_MEMORY;

int jwdpmi_main(const std::vector<std::string_view>&);

namespace jw
{
    namespace debug::detail
    {
        void setup_gdb_interface(io::rs232_config);
        void notify_gdb_exit(byte result);
    }

    void print_exception(const std::exception& e, int level) noexcept
    {
        std::cerr << "Exception " << std::dec << level << ": " << e.what() << '\n';
        if (auto* cpu_ex = dynamic_cast<const dpmi::cpu_exception*>(&e)) cpu_ex->print();
        try { std::rethrow_if_nested(e); }
        catch (const std::exception& e) { print_exception(e, level + 1); }
        catch (...) { std::cerr << "Exception " << std::dec << (level + 1) << ": Unknown exception.\n"; }
    }

    [[noreturn]] void terminate()
    {
        throw terminate_exception { };
    }

    [[noreturn]] void terminate_handler() noexcept
    {
        static unsigned terminated = 0;
        ++terminated;
        if (terminated == 2)
        {
            std::cerr << "Re-entry in std::terminate!\n";
            std::abort();
        }
        else if (terminated > 2)
        {
            do { asm("cli; hlt"); } while (true);
        }
        dpmi::ring0_privilege::force_leave();
        debug::break_with_signal(SIGTERM);
        std::cerr << video::ansi::set_80x50_mode();
        if (io::ps2_interface::instantiated()) io::ps2_interface::instance().reset();
        if (auto exc = std::current_exception())
        {
            std::cerr << "std::terminate called after throwing an exception:\n";
            try { std::rethrow_exception(exc); }
            catch (const std::exception& e) { print_exception(e); }
            catch (const terminate_exception& e) { std::cerr << "terminate_exception.\n"; }
            catch (...) { std::cerr << "unknown exception.\n"; }
        }
        else std::cerr << "Terminating.\n";
        debug::print_backtrace();

        std::_Exit(-1);
    }

    int exit_code { -1 };
}

[[gnu::force_align_arg_pointer]]
int main(int argc, const char** argv)
{
    _crt0_startup_flags &= ~_CRT0_FLAG_LOCK_MEMORY;
    try
    {
        std::vector<std::string_view> args { };
        args.reserve(argc);
        for (auto i = 1; i < argc; ++i)
        {
#           ifndef NDEBUG
            if (stricmp(argv[i], "--debug") == 0)
            {
                io::rs232_config cfg;
                cfg.set_com_port(io::com1);
                debug::detail::setup_gdb_interface(cfg);
            }
            else if (stricmp(argv[i], "--ext-debug") == 0)
            {
                debug::detail::debug_mode = true;
            }
            else
#           endif
            {
                args.emplace_back(argv[i]);
            }
        }

        if (debug::debug())
        {
            std::cout << "Debug mode activated. Connecting to GDB..." << std::endl;
            debug::breakpoint();
        }

        jw::exit_code = jwdpmi_main(args);
    }
    catch (const std::exception& e) { std::cerr << "Caught exception in main()!\n"; jw::print_exception(e); }
    catch (const jw::terminate_exception& e) { std::cerr << e.what() << '\n'; }
    catch (...) { std::cerr << "Caught unknown exception in main()!\n"; }

    for (auto& t : thread::detail::scheduler::threads) t->abort();
    auto thread_queue_copy = thread::detail::scheduler::threads;
    for (auto& t : thread_queue_copy)
    {
        while (t->is_running() || t->pending_exceptions() > 0)
        {
            try
            {
                static_cast<thread::detail::task_base*>(t.get())->abort(true);
            }
            catch (const std::exception& e) { std::cerr << "Caught exception from thread!\n"; jw::print_exception(e); }
            catch (const jw::terminate_exception& e) { }
            catch (...) { std::cerr << "Caught unknown exception from thread!\n"; }
        }
    }

    return jw::exit_code;
}

namespace jw
{
    constinit dpmi::locked_pool_resource<true>* irq_alloc { nullptr };
    constinit std::atomic_flag irq_alloc_resize { false };
    constexpr std::size_t irq_alloc_size = config::interrupt_initial_memory_pool;
    constinit std::size_t min_chunk_size { 0 };

    struct init
    {
        init() noexcept
        {
            using namespace jw::dpmi;
            using namespace jw::dpmi::detail;

            try
            {
                min_chunk_size = irq_alloc_size;
                irq_alloc = new dpmi::locked_pool_resource<true> { irq_alloc_size };
            }
            catch (...) { std::abort(); }

            setup_exception_throwers();

            fpu_context_switcher.reset(new fpu_context_switcher_t { });

            // Try setting control registers first in ring 3.  If we have no ring0 access, the
            // dpmi host might still trap and emulate control register access.
            std::optional<ring0_privilege> r0;
        retry:
            try { set_control_registers(); }
            catch (const cpu_exception&)
            {
                if (not r0 and ring0_privilege::wont_throw())
                {
                    r0.emplace();
                    goto retry;
                }
                // Setting cr0 or cr4 failed.  If compiled with SSE then this might be a problem.
                // For now, assume that the dpmi server already enabled these bits (HDPMI does this).
                // If not, then we'll soon crash with an invalid opcode on the first SSE instruction.
            }

            original_terminate_handler = std::set_terminate(terminate_handler);
        }

        ~init() noexcept
        {
            if (debug::debug()) debug::detail::notify_gdb_exit(exit_code);
            std::set_terminate(original_terminate_handler);
            jw::dpmi::detail::fpu_context_switcher.reset();
        }

    private:
        [[gnu::noipa]] static void set_control_registers()
        {
            std::uint32_t cr;
            asm ("mov %0, cr0" : "=r" (cr));
            asm ("mov cr0, %0" :: "r" (cr | 0x10));       // enable native x87 exceptions
            if constexpr (sse)
            {
                asm ("mov %0, cr4" : "=r" (cr));
                asm ("mov cr4, %0" :: "r" (cr | 0x600));  // enable SSE and SSE exceptions
            }
        }

        std::terminate_handler original_terminate_handler;
    } [[gnu::init_priority(102)]] initializer;

    [[nodiscard]] void* realloc(void* p, std::size_t new_size, std::size_t align)
    {
        void* const new_p = ::operator new(new_size, std::align_val_t { align });
        if (p != nullptr) [[likely]]
        {
            const auto old_size = [p]
            {
                if (irq_alloc != nullptr and irq_alloc->in_pool(p)) return irq_alloc->size(p);
                auto* const q = static_cast<std::uint8_t*>(p);
                return *reinterpret_cast<std::size_t*>(q - *(q - 1));
            }();
            std::memcpy(new_p, p, old_size);
            ::operator delete(p);
        }
        return new_p;
    }
}

extern "C"
{
    decltype(std::malloc) __real_malloc;
    decltype(std::free) __real_free;

    void* __wrap_malloc(std::size_t n) noexcept
    {
        constinit static std::atomic_flag in_malloc { false };
        // Fail here on re-entry.  This happens when an exception is thrown in
        // operator new, and malloc is called to allocate the exception.
        // Returning nullptr ensures the exception is allocated from an emergency
        // pool instead.
        if (in_malloc.test_and_set()) [[unlikely]] return nullptr;
        struct x { ~x() { in_malloc.clear(); } } scope_guard;
        try { return ::operator new(n); }
        catch (const std::bad_alloc&) { return nullptr; }
    }

    void* __wrap_realloc(void* p, std::size_t n) noexcept
    {
        try { return jw::realloc(p, n, __STDCPP_DEFAULT_NEW_ALIGNMENT__); }
        catch (const std::bad_alloc&) { return p; }
    }

    void* __wrap_calloc(std::size_t n, std::size_t size) noexcept { return __wrap_malloc(n * size); }

    void __wrap_free(void* p) noexcept { ::operator delete(p); }
}

[[nodiscard]] void* operator new(std::size_t size, std::align_val_t alignment)
{
    if (dpmi::in_irq_context() or dpmi::get_cs() == dpmi::detail::ring0_cs)
    {
        if (irq_alloc != nullptr)
        {
            debug::trap_mask dont_trap_here { };
            auto* p = irq_alloc->allocate(size, static_cast<std::size_t>(alignment));
            min_chunk_size = std::min(min_chunk_size, irq_alloc->max_chunk_size());
            return p;
        }
        else throw std::bad_alloc { };
    }

    if (min_chunk_size < irq_alloc_size / 4
        and irq_alloc != nullptr
        and not irq_alloc_resize.test_and_set()) [[unlikely]]
    {
        struct x { ~x() { irq_alloc_resize.clear(); } } scope_guard;
        dpmi::interrupt_mask no_interrupts_here { };
        debug::trap_mask dont_trap_here { };
        auto* ia = irq_alloc;
        irq_alloc = nullptr;
        try
        {
            ia->grow(irq_alloc_size / 2);
            min_chunk_size = ia->max_chunk_size();
        }
        catch (const std::bad_alloc&) { } // Relatively safe to ignore.
        irq_alloc = ia;
    }

    auto do_malloc = [](std::size_t n)
    {
        void* const p = __real_malloc(n);
        if (p == nullptr) throw std::bad_alloc { };
        return p;
    };

    const auto align = static_cast<std::size_t>(alignment);
    const auto overhead = sizeof(std::size_t) + sizeof(std::uint8_t);
    const auto n = size + align + overhead;

    void* const p_malloc = do_malloc(n);
    const auto p = reinterpret_cast<std::uintptr_t>(p_malloc);

    const auto a = p + overhead;
    auto b = a & -align;
    if (b != a) b += align;
    auto* const p_aligned = reinterpret_cast<void*>(b);
    *reinterpret_cast<std::size_t*>(p) = size;          // Store original size (for realloc).
    *reinterpret_cast<std::uint8_t*>(b - 1) = b - p;    // Store alignment offset.

    return p_aligned;
}

void operator delete(void* ptr, std::size_t n, std::align_val_t a) noexcept
{
    if (irq_alloc != nullptr and irq_alloc->in_pool(ptr))
    {
        debug::trap_mask dont_trap_here { };
        irq_alloc->deallocate(ptr, n, static_cast<std::size_t>(a));
    }
    else
    {
        auto* p = static_cast<std::uint8_t*>(ptr);
        p -= *(p - 1);
        __real_free(p);
    }
}

[[nodiscard]] void* operator new(std::size_t n)
{
    return ::operator new(n, std::align_val_t { __STDCPP_DEFAULT_NEW_ALIGNMENT__ });
}

void operator delete(void* p) noexcept
{
    ::operator delete(p, 1, std::align_val_t { });
}

void operator delete(void* p, std::size_t) noexcept
{
    ::operator delete(p);
}

void operator delete(void* p, std::align_val_t) noexcept
{
    ::operator delete(p);
}
