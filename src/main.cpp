/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2017 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#include <cstring>
#include <string_view>
#include <vector>
#include <crt0.h>
#include <sys/exceptn.h>
#include <csignal>
#include <fmt/core.h>
#include <jw/main.h>
#include <jw/alloc.h>
#include <jw/debug.h>
#include <jw/dpmi/cpu_exception.h>
#include <jw/io/rs232.h>
#include <jw/io/ps2_interface.h>
#include <jw/dpmi/ring0.h>
#include <jw/dpmi/detail/selectors.h>
#include <jw/dpmi/cpuid.h>
#include <jw/dpmi/bda.h>
#include <jw/video/ansi.h>
#include <cxxabi.h>
#include <unwind.h>
#include "jwdpmi_config.h"

using namespace jw;

extern "C"
{
    int _crt0_startup_flags = 0
        | config::user_crt0_startup_flags
        | _CRT0_FLAG_NMI_SIGNAL
        | _CRT0_DISABLE_SBRK_ADDRESS_WRAP
        | _CRT0_FLAG_NONMOVE_SBRK
        | _CRT0_FLAG_LOCK_MEMORY;

    extern unsigned __djgpp_stack_top;
    extern unsigned __djgpp_stack_limit;
}

namespace jw
{
    static constinit std::optional<dpmi::realmode_interrupt_handler> int2f_handler { std::nullopt };

    namespace debug::detail
    {
        void setup_gdb_interface(io::rs232_config);
        void notify_gdb_exit(byte result);
    }

    static void do_print_exception(const std::exception& e, int level = 0) noexcept
    {
        if (level == 0)
            fmt::print(stderr, "Exception: {}\n", e.what());
        else
            fmt::print(stderr, "Nested exception {:d}: {}\n", level, e.what());

        if (auto* exc = dynamic_cast<const dpmi::cpu_exception*>(&e))
            exc->print();

        if (auto* exc = dynamic_cast<const debug::assertion_failed*>(&e))
            exc->print();

        try { std::rethrow_if_nested(e); }
        catch (const std::exception& e) { do_print_exception(e, level + 1); }
        catch (...) { fmt::print(stderr, "Nested exception {:d}: unknown exception\n", level + 1); }
    }

    void print_exception()
    {
        try { throw; }
        catch (const std::exception& e) { do_print_exception(e); }
        catch (const abi::__forced_unwind&) { fmt::print(stderr, "Exception: __forced_unwind\n"); throw; }
        catch (...) { fmt::print(stderr, "Exception: unknown exception\n"); }
    }

    static void terminate_cleanup() noexcept
    {
        dpmi::ring0_privilege::force_leave();
        debug::break_with_signal(SIGTERM);

        // Exit will crash if int 2f is still hooked.
        int2f_handler.reset();

        // Make sure at least the keyboard works.
        io::ps2_interface::instance().reset();
    }

    [[noreturn]] static void terminate_handler() noexcept
    {
        static unsigned terminated = 0;
        ++terminated;
        if (terminated == 2)
        {
            fmt::print(stderr, "Re-entry in std::terminate!\n");
            std::abort();
        }
        else if (terminated > 2)
        {
            halt();
        }

        terminate_cleanup();

        if (auto e = std::current_exception())
        {
            fmt::print(stderr, "std::terminate called after throwing an exception:\n");
            try { std::rethrow_exception(e); }
            catch (...) { print_exception(); }
        }
        else fmt::print(stderr, "Terminating.\n");
        debug::stacktrace<64>::current(3).print();

        using namespace ::jw::dpmi::detail;
        auto* id = interrupt_id::get();
        if (id->next != nullptr)
        {
            fmt::print(stderr, "Currently servicing ");
            switch (id->type)
            {
            case interrupt_type::realmode_irq:  fmt::print(stderr, "real-mode IRQ callback"); break;
            case interrupt_type::exception:     fmt::print(stderr, "CPU exception 0x{:0>2x}", id->num); break;
            case interrupt_type::irq:           fmt::print(stderr, "IRQ {:d}", id->num); break;
            case interrupt_type::none:          fmt::print(stderr, "no interrupt (?)"); break;
            }
            fmt::print(stderr, ", unable to terminate.\n");
            halt();
        }

        uninstall_exception_handlers();

        std::_Exit(-1);
    }

    [[noreturn]] static void signal_handler(int sig) noexcept
    {
        terminate_cleanup();
        std::signal(sig, SIG_DFL);
        std::raise(sig);
        __builtin_unreachable();
    }

    // DPMI yield function replacement: int 2f, ax=1680
    static bool int2f(dpmi::realmode_registers* reg)
    {
        if (reg->ax != 0x1680) return false;
        if (dpmi::in_irq_context()) return false;
        [[maybe_unused]] std::conditional_t<config::save_fpu_on_realmode_callback, empty, dpmi::fpu_context> fpu { };
        detail::scheduler::safe_yield();
        errno = 0;
        reg->al = 0;
        return true;
    }

#ifndef NDEBUG
    static void initial_breakpoint()
    {
        fmt::print(stderr, "Debug mode activated.  Connecting to GDB...\n");
        debug::breakpoint();
    }

    static bool debug_from_main = false;
#endif

    int exit_code { -1 };
}

[[gnu::noipa, gnu::weak]]
int jwdpmi_main(std::span<std::string_view>)
{
    return -1;
}

extern "C" [[gnu::noipa, gnu::weak]]
int main(int argc, const char** argv)
{
    std::string_view args[argc];
    auto* a = args;
    for (auto i = 0; i < argc; ++i)
        *a++ = argv[i];

    const std::size_t n = a - args;
    return jwdpmi_main({ args, n });
}

extern "C" int __real_main(int, const char**);

extern "C" [[gnu::force_align_arg_pointer]]
int __wrap_main(int argc, const char** argv)
{
    try
    {
        int2f_handler.emplace(0x2f, [](dpmi::realmode_registers* reg, dpmi::far_ptr32) { return int2f(reg); });

#ifndef NDEBUG
        if (debug_from_main)
            initial_breakpoint();
#endif

        jw::exit_code = __real_main(argc, argv);
    }
    catch (const abi::__forced_unwind&)
    {
        detail::scheduler::catch_forced_unwind();
        fmt::print(stderr, "Terminating via forced unwind.\n");
    }
    catch (...)
    {
        fmt::print(stderr, "Caught exception from main():\n");
        jw::print_exception();
    }

    int2f_handler.reset();
    jw::detail::scheduler::kill_all();

    return jw::exit_code;
}

namespace jw
{
    constinit static std::optional<dpmi::mapped_dos_memory<dpmi::bios_data_area>> bda_memory;
    constinit static dpmi::locked_pool_resource* irq_alloc { nullptr };
    constinit static std::atomic_flag irq_alloc_resize { false };
    constexpr static std::size_t irq_alloc_size = config::global_locked_pool_size;
    constinit static std::size_t min_chunk_size { 0 };

    struct init
    {
        init() noexcept
        {
            using namespace jw::dpmi;
            using namespace jw::dpmi::detail;

            std::set_terminate(terminate_handler);
            std::signal(SIGABRT, signal_handler);

            // We can lock memory ourselves from this point on.
            _crt0_startup_flags &= ~_CRT0_FLAG_LOCK_MEMORY;

            // Stack has been allocated in locked memory, no need for that.
            const auto stack_begin = round_up_to_page_size(near_to_linear(__djgpp_stack_limit));
            const auto stack_end = round_down_to_page_size(near_to_linear(__djgpp_stack_top));
            if (stack_end > stack_begin)
                dpmi::linear_memory { stack_begin, stack_end - stack_begin }.unlock();

            if constexpr (not config::support_virtual_interrupt_flag)
            {
                bool ok { true };
                {
                    interrupt_unmask allow_irq { };
                    ok &= interrupts_enabled();
                    {
                        interrupt_mask no_irq { };
                        ok &= not interrupts_enabled();
                    }
                    ok &= interrupts_enabled();
                }
                if (not ok)
                {
                    const auto iopl = cpu_flags::current().io_privilege;
                    const auto cpl = selector_bits(get_cs()).privilege_level;
                    fmt::print(stderr, "Virtual interrupt support required on this host - see jwdpmi_config.h. (IOPL {} < CPL {})\n", iopl, cpl);
                    std::_Exit(-1);
                }
            }

            min_chunk_size = irq_alloc_size;
            locking_allocator<locked_pool_resource> irq_alloc_alloc { };
            irq_alloc = new (irq_alloc_alloc.allocate(1)) dpmi::locked_pool_resource { irq_alloc_size };

            const_cast<volatile selector&>(safe_ds) = __djgpp_ds_alias;
            const_cast<volatile selector&>(main_cs) = get_cs();
            const_cast<volatile selector&>(main_ds) = get_ds();

            cpuid::setup();
            asm volatile ("" ::: "memory");
            const auto cpu = dpmi::cpuid::feature_flags();
            const_cast<volatile bool&>(use_fxsave) = cpu.fxsave;

            asm volatile
            (R"(
                fnclex
                fninit
                sub esp, 4
                fnstcw [esp]
                or byte ptr [esp], 0xBF     # mask all FPU exceptions
                fldcw [esp]
                add esp, 4
            )");

            if (cpu.sse)
            {
                asm volatile
                (R"(
                    sub esp, 4
                    stmxcsr [esp]
                    or word ptr [esp], 0x1F80
                    ldmxcsr [esp]
                    add esp, 4
                )");
            }

            interrupt_id::setup();
            jw::detail::scheduler::setup();
            setup_exception_handling();
            setup_direct_ldt_access();

            bda_memory.emplace(1, far_ptr16 { 0x0040, 0x0000 });
            const_cast<bios_data_area* volatile&>(bda) = bda_memory->near_pointer();

            // Try setting control registers first in ring 3.  If we have no ring0 access, the
            // dpmi host might still trap and emulate control register access.
            bool use_ring0 = false;
        retry:
            try
            {
                std::optional<ring0_privilege> r0;
                if (use_ring0) r0.emplace();
                set_control_registers(cpu.sse);
            }
            catch (const general_protection_fault&)
            {
                if (not use_ring0 and ring0_privilege::wont_throw())
                {
                    use_ring0 = true;
                    goto retry;
                }
                // Setting cr0 or cr4 failed.  If compiled with SSE then this might be a problem.
                // For now, assume that the dpmi server already enabled these bits (HDPMI does this).
                // If not, then we'll soon crash with an invalid opcode on the first SSE instruction.
            }

#ifndef NDEBUG
            if (const auto* const debugopt = std::getenv("JWDPMI_DEBUG"))
            {
                const std::string_view opt { debugopt };
                auto init_gdb = []
                {
                    io::rs232_config cfg;
                    cfg.set_com_port(io::com1);
                    debug::detail::setup_gdb_interface(cfg);
                };

                if (opt == "early")
                {
                    init_gdb();
                    initial_breakpoint();
                }
                else if (opt == "main")
                {
                    init_gdb();
                    debug_from_main = true;
                }
                else if (opt == "nobreak")
                {
                    init_gdb();
                }
                else if (opt == "ext")
                {
                    debug::detail::debug_mode = true;
                }
                else fmt::print(stderr, "Warning: unknown debug option \"{}\"\n", opt);
            }
#endif
        }

        ~init() noexcept
        {
            if (debug::debug())
                debug::detail::notify_gdb_exit(exit_code);
        }

    private:
        [[gnu::noipa]] static void may_throw() { }
        [[gnu::noipa]] static void set_control_registers(bool sse)
        {
            may_throw();    // HACK
            std::uint32_t cr;
            asm ("mov %0, cr0" : "=r" (cr));
            asm volatile ("mov cr0, %0" :: "r" (cr | 0x10));    // enable native x87 exceptions

            if (sse or not config::support_virtual_interrupt_flag)
            {
                asm ("mov %0, cr4" : "=r" (cr));
                if constexpr (not config::support_virtual_interrupt_flag)
                {
                    if (cr & 2) // Protected-mode Virtual Interrupts flag
                    {
                        fmt::print(stderr, "CR4.PVI enabled, but not supported.\n");
                        std::terminate();
                    }
                }
                if (sse)
                {
                    asm volatile ("mov cr4, %0" :: "r" (cr | 0x600));    // enable SSE and SSE exceptions
                }
            }
        }
    };
    [[gnu::init_priority(102)]] init initializer;

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
            std::memcpy(new_p, p, std::min(old_size, new_size));
            ::operator delete(p);
        }
        return new_p;
    }

    [[nodiscard]] static void* do_locked_alloc(std::size_t size, std::size_t alignment)
    {
        if (irq_alloc == nullptr) throw std::bad_alloc { };

        debug::trap_mask dont_trap_here { };
        auto* p = irq_alloc->allocate(size, alignment);
        min_chunk_size = std::min(min_chunk_size, irq_alloc->max_chunk_size());
        return p;
    }

    static void resize_irq_alloc() noexcept
    {
        if (min_chunk_size >= irq_alloc_size / 4) [[likely]] return;
        if (irq_alloc == nullptr) return;
        if (irq_alloc_resize.test_and_set()) return;

        finally scope_guard { [] { irq_alloc_resize.clear(); } };
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
}

extern "C"
{
    decltype(std::malloc) __real_malloc;
    decltype(std::free) __real_free;
    decltype(abi::__cxa_allocate_exception) __real___cxa_allocate_exception;
    decltype(abi::__cxa_free_exception) __real___cxa_free_exception;
    decltype(abi::__cxa_allocate_dependent_exception) __real___cxa_allocate_dependent_exception;
    decltype(abi::__cxa_free_dependent_exception) __real___cxa_free_dependent_exception;

    void* __wrap_malloc(std::size_t n) noexcept
    {
        constinit static std::atomic_flag in_malloc { false };
        // Fail here on re-entry.  This happens when an exception is thrown in
        // operator new, and malloc is called to allocate the exception.
        // Returning nullptr ensures the exception is allocated from an emergency
        // pool instead.
        if (in_malloc.test_and_set()) [[unlikely]] return nullptr;
        finally scope_guard { [] { in_malloc.clear(); } };
        try { return ::operator new(n); }
        catch (const std::bad_alloc&) { return nullptr; }
    }

    void* __wrap_realloc(void* p, std::size_t n) noexcept
    {
        try { return jw::realloc(p, n, __STDCPP_DEFAULT_NEW_ALIGNMENT__); }
        catch (const std::bad_alloc&) { return p; }
    }

    void* __wrap_calloc(std::size_t n, std::size_t size) noexcept
    {
        auto num_bytes = static_cast<std::uint64_t>(n) * size;
        if (num_bytes > 0xffffffff) [[unlikely]] return nullptr;
        n = static_cast<std::size_t>(num_bytes);
        void* p = __wrap_malloc(n);
        if (p != nullptr) [[likely]] std::memset(p, 0, n);
        return p;
    }

    void* __wrap_memalign(std::size_t a, std::size_t n) noexcept
    {
        try { return ::operator new(n, std::align_val_t { a }); }
        catch (const std::bad_alloc&) { return nullptr; }
    }

    void __wrap_free(void* p) noexcept { ::operator delete(p); }

    void* __wrap___cxa_allocate_exception(std::size_t n) noexcept
    {
        dpmi::interrupt_mask no_irqs { };
        dpmi::async_signal_mask no_signals { };
        debug::trap_mask no_trap { };
        return __real___cxa_allocate_exception(n);
    }

    void __wrap___cxa_free_exception(void* p) noexcept
    {
        dpmi::interrupt_mask no_irqs { };
        dpmi::async_signal_mask no_signals { };
        debug::trap_mask no_trap { };
        return __real___cxa_free_exception(p);
    }

    abi::__cxa_dependent_exception* __wrap___cxa_allocate_dependent_exception() noexcept
    {
        dpmi::interrupt_mask no_irqs { };
        dpmi::async_signal_mask no_signals { };
        debug::trap_mask no_trap { };
        return __real___cxa_allocate_dependent_exception();
    }

    void __wrap___cxa_free_dependent_exception(abi::__cxa_dependent_exception* p) noexcept
    {
        dpmi::interrupt_mask no_irqs { };
        dpmi::async_signal_mask no_signals { };
        debug::trap_mask no_trap { };
        return __real___cxa_free_dependent_exception(p);
    }
}

namespace jw
{

    void* allocate_locked(std::size_t n, std::size_t a)
    {
        if (not dpmi::in_irq_context())
            resize_irq_alloc();

        return do_locked_alloc(n, a);
    }

    void free_locked(void* p, std::size_t n, std::size_t a)
    {
        debug::trap_mask dont_trap_here { };
        irq_alloc->deallocate(p, n, a);
    }

    void* allocate(std::size_t size, std::size_t alignment)
    {
        const auto align = std::max(alignment, std::size_t { 4 });
        const auto overhead = sizeof(std::size_t) + sizeof(std::uint8_t);
        const auto n = size + align + overhead;

        void* const p_malloc = __real_malloc(n);
        if (p_malloc == nullptr) throw std::bad_alloc { };
        const auto p = reinterpret_cast<std::uintptr_t>(p_malloc);

        const auto a = p + overhead;
        const auto b = (a + align - 1) & -align;
        auto* const p_aligned = reinterpret_cast<void*>(b);
        *reinterpret_cast<std::size_t*>(p) = size;          // Store original size (for realloc).
        *reinterpret_cast<std::uint8_t*>(b - 1) = b - p;    // Store alignment offset.

        return p_aligned;
    }

    void free(void* ptr, std::size_t, std::size_t)
    {
        auto* p = static_cast<std::uint8_t*>(ptr);
        p -= *(p - 1);
        __real_free(p);
    }
}

[[nodiscard]] void* operator new(std::size_t size, std::align_val_t alignment)
{
    if (dpmi::in_irq_context() or dpmi::get_cs() == dpmi::detail::ring0_cs)
        return do_locked_alloc(size, static_cast<std::size_t>(alignment));

    resize_irq_alloc();

    return allocate(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr, std::size_t n, std::align_val_t a) noexcept
{
    if (irq_alloc != nullptr and irq_alloc->in_pool(ptr))
        free_locked(ptr, n, static_cast<std::size_t>(a));
    else
        free(ptr, n, static_cast<std::size_t>(a));
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
