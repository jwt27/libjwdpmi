/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2016 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#include <jw/main.h>
#include <jw/detail/scheduler.h>
#include <jw/dpmi/cpu_exception.h>
#include <jw/dpmi/async_signal.h>
#include <jw/dpmi/detail/interrupt_id.h>
#include <jw/dpmi/detail/selectors.h>
#include <jw/dpmi/ring0.h>
#include <jw/sso_vector.h>
#include <sys/nearptr.h>
#include <cstring>
#include <vector>
#include <bitset>

namespace jw::dpmi::detail
{
    union trampoline_block
    {
        trampoline_block* next_free;
        alignas(exception_trampoline) std::byte data[sizeof(exception_trampoline)];
    };

    static constinit std::array<trampoline_block, 256> trampoline_pool;
    static constinit trampoline_block* free_list { nullptr };
    static constinit std::optional<sso_vector<std::exception_ptr, 3>> pending_exceptions { std::nullopt };
    static constinit std::array<std::optional<exception_handler>, 0x1f> exception_handlers { };
    static constinit std::bitset<async_signal::max_signals> available_signals { ~ std::uint64_t { 0 } };
    static constinit std::bitset<async_signal::max_signals> pending_signals { 0 };
    alignas (0x10) static constinit std::array<std::byte, config::exception_stack_size> exception_stack;

    template<typename T>
    static T* allocate_trampoline()
    {
        auto* const p = free_list;
        if (p == nullptr) throw std::runtime_error { "Trampoline pool exhausted" };
        free_list = p->next_free;
        return reinterpret_cast<T*>(p);
    }

    template<typename T>
    static void deallocate_trampoline(T* t) noexcept
    {
        auto* const p = reinterpret_cast<trampoline_block*>(t);
        p->next_free = free_list;
        free_list = p;
    }

    [[noreturn]]
    static void rethrow_cpu_exception()
    {
        if (pending_exceptions->empty() or not pending_exceptions->back())
            terminate();
        try
        {
            finally pop { [] { pending_exceptions->pop_back(); } };
            std::rethrow_exception(std::move(pending_exceptions->back()));
        }
        catch (cpu_exception& exc)
        {
            exc.stacktrace = exc.stacktrace.current(3);
            throw;
        }
    }

    static bool is_async_signal(const exception_info& info)
    {
        if (info.num != exception_num::general_protection_fault and
            info.num != exception_num::stack_segment_fault)
            return false;
        if (info.frame->flags.v86_mode)
            return false;

        std::size_t limit;
        asm("lsl %0, %1" : "=r" (limit) : "m" (main_ds));

        return limit < 0x1000;
    }

    inline bool handle_async_signal(const exception_info& info)
    {
        const auto id = pending_signals._Find_first();
        if (id == pending_signals.size())
            return false;

        pending_signals[id] = false;

        finally cleanup { []
        {
            if (pending_signals.none())
               descriptor::set_limit(main_ds, __djgpp_selector_limit);
        } };

        auto& slot = async_signal::slots[id];
        if (slot.valid())
            slot(info);

        return true;
    }

    [[gnu::cdecl]]
    static bool handle_exception(raw_exception_frame* frame) noexcept
    {
        constexpr bool save_fpu { config::save_fpu_on_exception };
        std::conditional_t<save_fpu, fpu_context, empty> fpu;

        auto* const data = frame->data;
        auto* const f = data->is_dpmi10 ? &frame->frame_10 : &frame->frame_09;
        interrupt_id id { &fpu, data->num, interrupt_type::exception };

        const exception_info info { data->num, &frame->reg, f, data->is_dpmi10 };

        bool success = false;
        try
        {
            if (is_async_signal(info))
                success = handle_async_signal(info);
            else
                success = data->func(info);
        }
        catch (...)
        {
            jw::detail::scheduler::catch_forced_unwind();
            if (config::enable_throwing_from_cpu_exceptions and
                redirect_exception(info, detail::rethrow_cpu_exception))
            {
                pending_exceptions->emplace_back(std::current_exception());
                success = true;
            }
            else
            {
                fmt::print(stderr, "Caught exception while handling CPU exception 0x{:0>2x}\n", data->num.value);
                try { print_exception(); }
                catch (const abi::__forced_unwind&) { }
                if (redirect_exception(info, terminate)) success = true;
                else std::terminate();
            }
        }

#       ifndef NDEBUG
        if (*reinterpret_cast<std::uint32_t*>(exception_stack.data()) != 0xdeadbeef)
            fmt::print(stderr, "Stack overflow handling exception 0x{:0>2x}\n", data->num.value);
#       endif

        asm ("cli");
        return success;
    }

    template<bool dpmi10_frame>
    [[gnu::naked]]
    static void exception_entry_point()
    {
#       pragma GCC diagnostic push
#       pragma GCC diagnostic ignored "-Winvalid-offsetof"
        asm
        (R"(
            pusha
            push ds; push es; push fs; push gs

            mov edx, cs:[%[ds]]
            mov ebx, ss
            mov es, edx
            cmp bx, dx
            je L%=keep_stack

            #   Copy frame to new stack
            xor ecx, ecx
            mov edi, %[stack]
            mov cl, %[frame_size] / 4
            mov ds, ebx
            lea esi, [esp + ecx * 4 - 4]
            std
            rep movsd
            add edi, 4
            add esi, 4

            #   Switch to the new stack
            mov ss, edx
            mov esp, edi
        L%=keep_stack:
            mov ds, edx
            mov ebp, esp
            push esp
            mov fs, ebx
            and esp, -0x10          # Align stack

            cld
            mov ss:[esp], ebp       # Pointer to raw_exception_frame
            call %[handle_exception]

            mov edx, ss
            cmp dx, bx
            je L%=ret_same_stack

            #   Copy frame and switch back to previous stack
            mov es, ebx
            mov ebp, esi
            xor ecx, ecx
            xchg edi, esi
            mov cl, %[frame_size] / 4
            cld
            rep movsd
            mov ss, ebx
        L%=ret_same_stack:
            mov esp, ebp
            pop gs; pop fs; pop es; pop ds
            test al, al             # Check return value
            popa
            jz L%=chain             # Chain if false
            add esp, %[frame_offset]
            retf

        L%=chain:
            #   Chain to next handler
            add esp, %[chain_offset]
            retf
         )" :
            :   [ds]                "i" (&safe_ds),
                [stack]             "i" (exception_stack.begin() + exception_stack.size() - 0x10),
                [frame_size]        "i" (sizeof(raw_exception_frame) - (dpmi10_frame ? 0 : sizeof(dpmi10_exception_frame))),
                [handle_exception]  "i" (handle_exception),
                [frame_offset]      "i" ((dpmi10_frame ? offsetof(raw_exception_frame, frame_10) : offsetof(raw_exception_frame, frame_09)) - offsetof(raw_exception_frame, data)),
                [chain_offset]      "i" (offsetof(raw_exception_frame, chain_to) - offsetof(raw_exception_frame, data))
        );
#       pragma GCC diagnostic pop
    }

    std::ptrdiff_t exception_trampoline::find_entry_point(bool dpmi10_frame) const noexcept
    {
        auto src = reinterpret_cast<intptr_t>(&entry_point + 1);
        auto dst = reinterpret_cast<intptr_t>(dpmi10_frame ? exception_entry_point<true> : exception_entry_point<false>);
        return dst - src;
    }

    exception_trampoline* exception_trampoline::allocate()
    {
        return allocate_trampoline<exception_trampoline>();
    }

    void exception_trampoline::deallocate(exception_trampoline* t)
    {
        deallocate_trampoline(t);
    }

    exception_trampoline::~exception_trampoline()
    {
        interrupt_mask no_irq { };

        if (auto* p = data->prev)
            p->data->next = data->next;

        if (auto* n = data->next)   // middle of chain
        {
            n->data->prev = data->prev;
            n->chain_to_segment = chain_to_segment;
            n->chain_to_offset = chain_to_offset;
        }
        else                        // last in chain
        {
            last[data->realmode][data->num] = data->prev;
            far_ptr32 chain_to { chain_to_segment, chain_to_offset };

            if (data->realmode)
                detail::cpu_exception_handlers::set_rm_handler(data->num, chain_to);
            else
                detail::cpu_exception_handlers::set_pm_handler(data->num, chain_to);
        }
        data->~exception_handler_data();
        data_alloc.deallocate(data, 1);
    }

    struct redirect_trampoline
    {
        redirect_trampoline(std::uintptr_t ret, cpu_flags fl, selector ss, void (*f)())
            : return_address { ret }
            , flags { fl }
            , ss { ss }
            , self { this }
            , stage2_offset { find_stage2() }
            , func { f }
        { }

        std::uintptr_t code() const noexcept
        {
            return reinterpret_cast<std::uintptr_t>(&push0_imm32);
        }

    private:
        std::ptrdiff_t find_stage2() const noexcept
        {
            auto src = reinterpret_cast<intptr_t>(&stage2_offset + 1);
            auto dst = reinterpret_cast<intptr_t>(stage2);
            return dst - src;
        }

        struct [[gnu::packed]] alignas(0x10)
        {
            const std::uint8_t push0_imm32 { 0x68 };
            std::uintptr_t return_address;
            const std::uint8_t push1_imm32 { 0x68 };
            cpu_flags flags;
            const std::uint8_t push2_imm32 { 0x68 };
            std::uint32_t ss;
            const std::uint8_t push3_imm32 { 0x68 };
            redirect_trampoline* self;
            const std::uint8_t jmp_rel32 { 0xe9 };
            std::ptrdiff_t stage2_offset;
            void (*func)();
        };

        [[gnu::naked]]
        static void stage2()
        {
            asm
            (R"(
            .cfi_signal_frame
            .cfi_def_cfa esp, 0x10
            .cfi_offset eflags, -0x08
            .cfi_offset ss, -0x0c
                push ss
            .cfi_def_cfa_offset 0x14
                push ss
            .cfi_def_cfa_offset 0x18
                pop ds
            .cfi_def_cfa_offset 0x14
                pop es
            .cfi_def_cfa_offset 0x10
                call %0
                add esp, 0x08
            .cfi_def_cfa_offset 0x08
                popf
            .cfi_def_cfa_offset 0x04
                ret
             )" :
                : "i" (stage3)
            );
        }

        [[gnu::no_caller_saved_registers, gnu::force_align_arg_pointer, gnu::cdecl]]
        static void stage3(redirect_trampoline* self, std::uint32_t ss, cpu_flags flags)
        {
            auto f = self->func;
            self->~redirect_trampoline();
            deallocate_trampoline(self);
            fpu_context fpu { };
            asm volatile ("push %0; popf" : : "rm" (flags) : "cc");
            asm volatile
            (R"(
                mov ss, %k0
                mov ds, %k1
                mov es, %k1
             )" : : "r" (ss), "r" (main_ds)
            );
            f();
        }
    };

    static_assert (sizeof(redirect_trampoline) <= sizeof(exception_trampoline));
    static_assert (alignof(redirect_trampoline) <= alignof(exception_trampoline));

    static bool default_exception_handler(const exception_info& i)
    {
        if (i.num == exception_num::double_fault or
            i.num == exception_num::machine_check)
        {
            i.frame->print();
            i.registers->print();
            fmt::print(stderr, "{}\n", cpu_category().message(i.num));
            halt();
        }

        if constexpr (not config::enable_throwing_from_cpu_exceptions) return false;

        try
        {
            throw_cpu_exception(i);
        }
        catch (...)
        {
            if (redirect_exception(i, rethrow_cpu_exception)) [[likely]]
            {
                pending_exceptions->emplace_back(std::current_exception());
                return true;
            }
        }
        return false;
    }

    void setup_exception_handling()
    {
        constinit static bool done { false };
        if (done) return;
        done = true;

        for (auto& i : trampoline_pool) i.next_free = &i + 1;
        trampoline_pool.back().next_free = nullptr;
        free_list = trampoline_pool.begin();
        *reinterpret_cast<std::uint32_t*>(exception_stack.data()) = 0xdeadbeef;

        pending_exceptions.emplace();

        auto install = [](exception_num n)
        {
            exception_handlers[n].emplace(n, [](const exception_info& i) { return default_exception_handler(i); });
        };

        auto try_install = [install](exception_num n)
        {
            try { install(n); } catch (const dpmi_error&) { /* ignore */ }
        };

        install(exception_num::general_protection_fault);
        install(exception_num::stack_segment_fault);

        if constexpr (not config::enable_throwing_from_cpu_exceptions) return;

        install(exception_num::divide_error);
        install(exception_num::trap);
        install(exception_num::non_maskable_interrupt);
        install(exception_num::breakpoint);
        install(exception_num::overflow);
        install(exception_num::bound_range_exceeded);
        install(exception_num::invalid_opcode);
        install(exception_num::device_not_available);
        install(exception_num::double_fault);
        install(exception_num::x87_segment_not_present);
        install(exception_num::invalid_tss);
        install(exception_num::segment_not_present);
        install(exception_num::page_fault);

        try_install(exception_num::x87_exception);
        try_install(exception_num::alignment_check);
        try_install(exception_num::machine_check);
        try_install(exception_num::sse_exception);
        try_install(exception_num::virtualization_exception);
        try_install(exception_num::security_exception);
    }

    void uninstall_exception_handlers()
    {
        for (auto& i : exception_handlers) i.reset();
    }
}

namespace jw::dpmi
{
    bool redirect_exception(const exception_info& info, void(*func)())
    {
        if (info.frame->info_bits.redirect_elsewhere) return false;
        if (info.frame->fault_address.segment != detail::main_cs) return false;
        if (info.frame->flags.v86_mode) return false;
        if (info.frame->stack.segment != detail::main_ds and
            info.frame->stack.segment != detail::safe_ds) return false;

        const std::uintptr_t ret = info.frame->fault_address.offset;
        const cpu_flags flags = info.frame->flags;
        const selector ss = info.frame->stack.segment;

        using namespace ::jw::dpmi::detail;
        auto* const p = allocate_trampoline<redirect_trampoline>();
        new (p) redirect_trampoline { ret, flags, ss, func };

        info.frame->stack.segment = safe_ds;
        info.frame->flags.interrupts_enabled = false;
        info.frame->fault_address.offset = p->code();
        info.frame->info_bits.redirect_elsewhere = true;
        return true;
    }

    void async_signal::raise(id_type i)
    {
        detail::pending_signals[i] = true;
        descriptor::set_limit(detail::main_ds, 0xfff);
    }

    async_signal::id_type async_signal::allocate_id()
    {
        auto i = detail::available_signals._Find_first();
        if (i == detail::available_signals.size()) throw std::runtime_error { "No more async_signal IDs available" };
        detail::available_signals[i] = false;
        return i;
    }

    async_signal::~async_signal()
    {
        detail::available_signals[id] = true;
    }

    void throw_cpu_exception(const exception_info& info)
    {
        switch (info.num)
        {
        case 0x00: throw specific_cpu_exception<0x00> { info };
        case 0x01: throw specific_cpu_exception<0x01> { info };
        case 0x02: throw specific_cpu_exception<0x02> { info };
        case 0x03: throw specific_cpu_exception<0x03> { info };
        case 0x04: throw specific_cpu_exception<0x04> { info };
        case 0x05: throw specific_cpu_exception<0x05> { info };
        case 0x06: throw specific_cpu_exception<0x06> { info };
        case 0x07: throw specific_cpu_exception<0x07> { info };
        case 0x08: throw specific_cpu_exception<0x08> { info };
        case 0x09: throw specific_cpu_exception<0x09> { info };
        case 0x0a: throw specific_cpu_exception<0x0a> { info };
        case 0x0b: throw specific_cpu_exception<0x0b> { info };
        case 0x0c: throw specific_cpu_exception<0x0c> { info };
        case 0x0d: throw specific_cpu_exception<0x0d> { info };
        case 0x0e: throw specific_cpu_exception<0x0e> { info };
        case 0x10: throw specific_cpu_exception<0x10> { info };
        case 0x11: throw specific_cpu_exception<0x11> { info };
        case 0x12: throw specific_cpu_exception<0x12> { info };
        case 0x13: throw specific_cpu_exception<0x13> { info };
        case 0x14: throw specific_cpu_exception<0x14> { info };
        case 0x1e: throw specific_cpu_exception<0x1e> { info };
        default: throw cpu_exception { info };
        }
    }

    std::string_view exception_num::message() const noexcept
    {
        switch (value)
        {
        case exception_num::divide_error:             return "Divide error";
        case exception_num::trap:                     return "Debug exception";
        case exception_num::non_maskable_interrupt:   return "Non-maskable interrupt";
        case exception_num::breakpoint:               return "Breakpoint";
        case exception_num::overflow:                 return "Overflow";
        case exception_num::bound_range_exceeded:     return "Bound range exceeded";
        case exception_num::invalid_opcode:           return "Invalid opcode";
        case exception_num::device_not_available:     return "Device not available";
        case exception_num::double_fault:             return "Double fault";
        case exception_num::x87_segment_not_present:  return "x87 Segment overrun";
        case exception_num::invalid_tss:              return "Invalid Task State Segment";
        case exception_num::segment_not_present:      return "Segment not present";
        case exception_num::stack_segment_fault:      return "Stack Segment fault";
        case exception_num::general_protection_fault: return "General protection fault";
        case exception_num::page_fault:               return "Page fault";
        case exception_num::x87_exception:            return "x87 Floating-point exception";
        case exception_num::alignment_check:          return "Alignment check";
        case exception_num::machine_check:            return "Machine check";
        case exception_num::sse_exception:            return "SSE Floating-point exception";
        case exception_num::virtualization_exception: return "Virtualization exception";
        case exception_num::security_exception:       return "Security exception";
        default:                                      return "";
        }
    }

    const std::error_category& cpu_category() noexcept
    {
        struct : public std::error_category
        {
            virtual const char* name() const noexcept override { return "CPU"; }
            virtual std::string message(int ev) const override
            {
                auto msg = exception_num { static_cast<std::uint8_t>(ev) }.message();
                if (msg.empty())
                    return fmt::format("Unknown CPU exception 0x{:0>2x}", ev);
                else
                    return { msg.begin(), msg.end() };
            }
        } static cat;
        return cat;
    }
}
