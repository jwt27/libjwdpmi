/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <cstring>
#include <string_view>
#include <vector>
#include <crt0.h>
#include <sys/exceptn.h>
#include <csignal>
#include <fmt/core.h>
#include <jw/main.h>
#include <jw/alloc.h>
#include <jw/debug/debug.h>
#include <jw/dpmi/cpu_exception.h>
#include <jw/debug/detail/signals.h>
#include <jw/io/rs232.h>
#include <jw/io/ps2_interface.h>
#include <jw/dpmi/ring0.h>
#include <jw/dpmi/detail/selectors.h>
#include <jw/dpmi/cpuid.h>
#include <jw/video/ansi.h>
#include <cxxabi.h>
#include <unwind.h>
#include "jwdpmi_config.h"

using namespace jw;

int _crt0_startup_flags = 0
| config::user_crt0_startup_flags
| _CRT0_FLAG_NMI_SIGNAL
| _CRT0_DISABLE_SBRK_ADDRESS_WRAP
| _CRT0_FLAG_NONMOVE_SBRK
| _CRT0_FLAG_LOCK_MEMORY;

int jwdpmi_main(std::span<std::string_view>);

namespace jw
{
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
        if (auto* cpu_ex = dynamic_cast<const dpmi::cpu_exception*>(&e)) cpu_ex->print();
        try { std::rethrow_if_nested(e); }
        catch (const std::exception& e) { do_print_exception(e, level + 1); }
        catch (...) { fmt::print(stderr, "Nested exception {:d}: unknown exception\n", level + 1); }
    }

    void print_exception() noexcept
    {
        try { throw; }
        catch (const std::exception& e) { do_print_exception(e); }
        catch (...) { fmt::print(stderr, "Exception: unknown exception\n"); }
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
        dpmi::ring0_privilege::force_leave();
        debug::break_with_signal(SIGTERM);
        if (io::ps2_interface::instantiated()) io::ps2_interface::instance().reset();
        if (auto exc = std::current_exception())
        {
            fmt::print(stderr, "std::terminate called after throwing an exception:\n");
            try { std::rethrow_exception(exc); }
            catch (const terminate_exception& e)
            {
                e.defuse();
                fmt::print(stderr, "terminate_exception\n");
            }
            catch (...) { print_exception(); }
        }
        else fmt::print(stderr, "Terminating.\n");
        debug::print_backtrace();

        using namespace ::jw::dpmi::detail;
        auto* id = interrupt_id::get();
        if (id->next != nullptr)
        {
            fmt::print(stderr, "Currently servicing ");
            switch (id->type)
            {
            case interrupt_type::realmode_irq:  fmt::print(stderr, "real-mode IRQ callback"); break;
            case interrupt_type::exception:     fmt::print(stderr, "CPU exception 0x{:0>2x}", id->num); break;
            case interrupt_type::irq:           fmt::print(stderr, "IRQ 0x{:0>2x}", id->num); break;
            case interrupt_type::none:          fmt::print(stderr, "no interrupt (?)"); break;
            }
            fmt::print(stderr, ", unable to terminate.\n");
            halt();
        }

        uninstall_exception_handlers();

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
        std::string_view args[argc];
        auto* a = args;
        new (a++) std::string_view { argv[0] };
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
                new (a++) std::string_view { argv[i] };
            }
        }

        if (debug::debug())
        {
            fmt::print(stderr, "Debug mode activated. Connecting to GDB...\n");
            debug::breakpoint();
        }

        const std::size_t n = a - args;
        jw::exit_code = jwdpmi_main({ args, n });
    }
    catch (const jw::terminate_exception& e) { e.defuse(); fmt::print(stderr, "{}\n", e.what()); }
    catch (...) { fmt::print(stderr, "Caught exception in main()!\n"); jw::print_exception(); }

    jw::detail::scheduler::kill_all();

    return jw::exit_code;
}

namespace jw::dpmi::detail
{
    const selector main_cs { };
    const selector main_ds { };
    const selector safe_ds { };
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

            std::set_terminate(terminate_handler);

            if constexpr (not config::support_virtual_interrupt_flag)
            {
                if (cpu_flags::current().io_privilege < 3)
                {
                    fmt::print(stderr, "IOPL 3 required.\n");
                    std::terminate();
                }
            }

            min_chunk_size = irq_alloc_size;
            irq_alloc = new dpmi::locked_pool_resource<true> { irq_alloc_size };

            const_cast<selector&>(safe_ds) = __djgpp_ds_alias;
            const_cast<selector&>(main_cs) = get_cs();
            const_cast<selector&>(main_ds) = get_ds();

            cpuid::setup();
            asm volatile ("" ::: "memory");
            const auto cpu = dpmi::cpuid::feature_flags();
            use_fxsave = cpu.fxsave;

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
        }

        ~init() noexcept
        {
            if (debug::debug()) debug::detail::notify_gdb_exit(exit_code);
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

        local_destructor scope_guard { [] { irq_alloc_resize.clear(); } };
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

    void* allocate_locked(std::size_t n, std::align_val_t a)
    {
        if (not dpmi::in_irq_context())
            resize_irq_alloc();

        return do_locked_alloc(n, static_cast<std::size_t>(a));
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
        local_destructor scope_guard { [] { in_malloc.clear(); } };
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
}

[[nodiscard]] void* operator new(std::size_t size, std::align_val_t alignment)
{
    if (dpmi::in_irq_context() or dpmi::get_cs() == dpmi::detail::ring0_cs)
        return do_locked_alloc(size, static_cast<std::size_t>(alignment));

    resize_irq_alloc();

    const auto align = std::max(static_cast<std::size_t>(alignment), std::size_t { 4 });
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
