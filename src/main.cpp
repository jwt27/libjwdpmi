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
    void print_exception(const std::exception& e, int level) noexcept
    {
        std::cerr << "Exception " << std::dec << level << ": " << e.what() << '\n';
        try { std::rethrow_if_nested(e); }
        catch (const std::exception& e) { print_exception(e, level + 1); }
        catch (...) { std::cerr << "Exception " << std::dec << (level + 1) << ": Unknown exception.\n"; }
    }

    extern "C" void* irq_safe_malloc(std::size_t n)
    {
        if (dpmi::in_irq_context() or dpmi::get_cs() == dpmi::detail::ring0_cs) return nullptr;
        else return std::malloc(n);
    }

    [[gnu::no_sanitize("undefined")]] void setup_irq_safe_exceptions()
    {
        auto p = reinterpret_cast<byte*>(abi::__cxa_allocate_exception);
        p = std::find(p, p + 0x20, 0xe8);
        auto call_offset = reinterpret_cast<volatile std::ptrdiff_t*>(p + 1);
        auto next_insn = reinterpret_cast<std::uintptr_t>(p + 5);               // e8 call instruction is 5 bytes

        if (*p != 0xe8 or reinterpret_cast<std::uintptr_t>(next_insn + *call_offset) != reinterpret_cast<std::uintptr_t>(std::malloc)) [[unlikely]] { asm ("int 3"); }

        *call_offset = reinterpret_cast<std::uintptr_t>(irq_safe_malloc) - next_insn;
    }

    [[noreturn]] void terminate()
    {
        throw terminate_exception { };
    }

    namespace debug
    {
        namespace detail
        {
            void setup_gdb_interface(io::rs232_config);
            void notify_gdb_exit(byte result);
        }
    }

    std::terminate_handler original_terminate_handler;
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

    struct init
    {
        init() noexcept
        {
            using namespace jw::dpmi;
            using namespace jw::dpmi::detail;

            setup_exception_throwers();

            fpu_context_switcher.reset(new fpu_context_switcher_t { });

            bool use_ring0 { false };
        retry_cr0:
            try     // This part is really tricky without irq-safe exceptions set up.
            {
                std::optional<ring0_privilege> r0;  // try to access control registers in ring 0
                if (use_ring0) r0 = ring0_privilege { };
                // if we have no ring0 access, the dpmi host might still trap and emulate control register access.

                set_control_registers();
            }
            catch (const cpu_exception&)
            {
                if (not use_ring0 and ring0_privilege::wont_throw())
                {
                    use_ring0 = true;
                    goto retry_cr0;
                }
                // setting cr0 or cr4 failed. if compiled with SSE then this might be a problem.
                // for now, assume that the dpmi server already enabled these bits (HDPMI does this).
                // if not, then we'll soon crash with an invalid opcode on the first SSE instruction.
            }

            original_terminate_handler = std::set_terminate(terminate_handler);
        }

        ~init() noexcept
        {
            if (debug::debug()) debug::detail::notify_gdb_exit(exit_code);
        }

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
    } initializer [[gnu::init_priority(101)]];
}

[[gnu::force_align_arg_pointer]]
int main(int argc, const char** argv)
{
    setup_irq_safe_exceptions();    // This should really be done in init() above.
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
            catch (...) { std::cerr << "Caught unknown exception from thread()!\n"; }
        }
    }

    return jw::exit_code;
}

namespace jw
{
    enum : unsigned { yes, no, almost } irq_alloc_initialized { no };
    dpmi::locked_pool_resource<true>* irq_alloc { nullptr };
    std::atomic_flag irq_alloc_resize { false };
    std::size_t half_irq_alloc_size;
    std::size_t min_chunk_size;
}

[[nodiscard]] void* operator new(std::size_t n, std::align_val_t alignment)
{
    if (dpmi::in_irq_context() or dpmi::get_cs() == dpmi::detail::ring0_cs)
    {
        if (irq_alloc_initialized == yes)
        {
            debug::trap_mask dont_trap_here { };
            auto* p = irq_alloc->allocate(n, static_cast<std::size_t>(alignment));
            min_chunk_size = std::min(min_chunk_size, irq_alloc->max_chunk_size());
            return p;
        }
        else throw std::bad_alloc { };
    }
    if (irq_alloc_initialized == no) [[unlikely]]
    {
        dpmi::interrupt_mask no_interrupts_here { };
        try
        {
            irq_alloc_initialized = almost;
            {
                debug::trap_mask dont_trap_here { };
                if (irq_alloc != nullptr) delete irq_alloc;
                irq_alloc = nullptr;
                half_irq_alloc_size = config::interrupt_initial_memory_pool / 2;
                irq_alloc = new dpmi::locked_pool_resource<true> { half_irq_alloc_size * 2 };
                min_chunk_size = half_irq_alloc_size * 2;
                irq_alloc_initialized = yes;
            }
        }
        catch (...)
        {
            irq_alloc_initialized = no;
            throw;
        }
    }
    else if (irq_alloc_initialized == yes
        and min_chunk_size < half_irq_alloc_size
        and not irq_alloc_resize.test_and_set()) [[unlikely]]
    {
        dpmi::interrupt_mask no_interrupts_here { };
        debug::trap_mask dont_trap_here { };
        try
        {
            irq_alloc_initialized = almost;
            irq_alloc->grow(half_irq_alloc_size);
            half_irq_alloc_size += half_irq_alloc_size / 2;
            min_chunk_size = irq_alloc->max_chunk_size();
            irq_alloc_initialized = yes;
        }
        catch (...)
        {
            irq_alloc_initialized = no;
            irq_alloc_resize.clear();
            throw;
        }
        irq_alloc_resize.clear();
    }

    auto align = std::max(static_cast<std::size_t>(alignment), sizeof(std::size_t));
    n += align + sizeof(void*);

    auto* p = std::malloc(n);
    if (p == nullptr) [[unlikely]] throw std::bad_alloc { };
    auto b = ((reinterpret_cast<std::uintptr_t>(p) + sizeof(void*)) & -align) + align;
    *(reinterpret_cast<void**>(b) - 1) = p;
    return reinterpret_cast<void*>(b);
}

[[nodiscard]] void* operator new(std::size_t n)
{
    return ::operator new(n, std::align_val_t { __STDCPP_DEFAULT_NEW_ALIGNMENT__ });
}

void operator delete(void* p, std::size_t n, std::align_val_t a) noexcept
{
    try
    {
        if (irq_alloc_initialized == yes and irq_alloc->in_pool(p))
        {
            debug::trap_mask dont_trap_here { };
            irq_alloc->deallocate(p, n, static_cast<std::size_t>(a));
            return;
        }
        p = *(reinterpret_cast<void**>(p) - 1);
        std::free(p);
    }
    catch (...)
    {
        std::cerr << "Caught exception in operator delete!\n";
        std::terminate();
    }
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
