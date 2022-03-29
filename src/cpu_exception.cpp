/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#include <jw/main.h>
#include <jw/dpmi/cpu_exception.h>
#include <jw/debug/debug.h>
#include <jw/dpmi/detail/interrupt_id.h>
#include <jw/dpmi/ring0.h>
#include <jw/sso_vector.h>
#include <cstring>
#include <vector>

namespace jw::dpmi::detail
{
    struct redirect_trampoline;

    using trampoline_allocator = monomorphic_allocator<basic_pool_resource, exception_trampoline>;
    using redirect_allocator = monomorphic_allocator<basic_pool_resource, redirect_trampoline>;

    [[gnu::section(".text.hot")]] alignas (exception_trampoline)
    static constinit std::array<std::byte, 8_KB> trampoline_pool;
    static constinit std::optional<basic_pool_resource> trampoline_memres { std::nullopt };
    static constinit std::optional<sso_vector<std::exception_ptr, 3>> pending_exceptions { std::nullopt };
    static constinit std::array<std::optional<exception_handler>, 0x1f> exception_throwers { };

    struct
    {
        selector ds;
        std::uint32_t stack_end;
        alignas (0x10) std::array<std::byte, config::exception_stack_size> stack;
    } static constinit exception_data;

    [[noreturn]]
    static void rethrow_cpu_exception()
    {
        auto e = std::move(pending_exceptions->back());
        pending_exceptions->pop_back();
        std::rethrow_exception(e);
    }

    [[gnu::cdecl, gnu::hot]]
    static bool handle_exception(raw_exception_frame* frame) noexcept
    {
        auto* data = frame->data;
        auto* const f = data->is_dpmi10 ? &frame->frame_10 : &frame->frame_09;
        interrupt_id id { data->num, interrupt_type::exception };
        if (id.fpu_context_switched) return true;

        const exception_info i { &frame->reg, f, data->is_dpmi10 };

        bool success = false;
        try
        {
            success = data->func(i);
        }
        catch (...)
        {
            const auto base = descriptor::get_base(get_cs());
            const bool can_redirect = not f->flags.v86_mode and
                                      descriptor::get_base(f->fault_address.segment) == base and
                                      descriptor::get_base(f->stack.segment) == base;
            const bool can_throw = config::enable_throwing_from_cpu_exceptions and can_redirect;

            if (can_throw)
            {
                pending_exceptions->emplace_back(std::current_exception());
                redirect_exception(f, detail::rethrow_cpu_exception);
                success = true;
            }
            else if (can_redirect)
            {
                try { throw; }
                catch (const cpu_exception& e) { e.print(); }
                catch (...)
                {
                    fmt::print(stderr, "Caught exception while handling CPU exception 0x{:0>2x}\n", data->num.value);
                    try { throw; }
                    catch (const std::exception& e) { print_exception(e); }
                    catch (...) { }
                }
                redirect_exception(f, kill);
                success = true;
            }
            else std::terminate();
        }

#       ifndef NDEBUG
        if (exception_data.stack_end != 0xDEADBEEF)
            fmt::print(stderr, "Stack overflow handling exception 0x{:0>2x}\n", data->num.value);
#       endif

        asm ("cli");
        return success;
    }

    template<bool dpmi10_frame>
    [[gnu::naked, gnu::hot]]
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
            :   [ds]                "i" (&exception_data.ds),
                [stack]             "i" (exception_data.stack.begin() + exception_data.stack.size() - 0x04),
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
        trampoline_allocator alloc { &*trampoline_memres };
        return std::allocator_traits<trampoline_allocator>::allocate(alloc, 1);
    }

    void exception_trampoline::deallocate(exception_trampoline* p)
    {
        trampoline_allocator alloc { &*trampoline_memres };
        std::allocator_traits<trampoline_allocator>::deallocate(alloc, p, 1);
    }

    exception_trampoline::~exception_trampoline()
    {
        if (data->next != nullptr)  // middle of chain
        {
            data->next->data->prev = data->prev;
            data->next->chain_to_segment = chain_to_segment;
            data->next->chain_to_offset = chain_to_offset;
        }
        else                        // last in chain
        {
            last[data->num] = data->prev;
            far_ptr32 chain_to { chain_to_segment, chain_to_offset };
            detail::cpu_exception_handlers::set_pm_handler(data->num, chain_to);
        }
        data->~exception_handler_data();
        data_alloc.deallocate(data, 1);
    }

    struct redirect_trampoline
    {
        redirect_trampoline(std::uintptr_t ret, cpu_flags fl, void (*f)())
            : return_address { ret }
            , flags { fl }
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
            redirect_trampoline* self;
            const std::uint8_t jmp_rel32 { 0xe9 };
            std::ptrdiff_t stage2_offset;
        };
        void (*func)();

        [[gnu::naked]]
        static void stage2()
        {
            asm
            (R"(
            .cfi_signal_frame
            .cfi_def_cfa esp, 0x0c
            .cfi_offset eflags, -0x08
                call %0
                add esp, 4
            .cfi_def_cfa_offset 0x08
                popf
            .cfi_restore eflags
            .cfi_def_cfa_offset 0x04
                ret
             )" :
                : "i" (stage3)
            );
        }

        [[gnu::no_caller_saved_registers, gnu::force_align_arg_pointer, gnu::cdecl]]
        static void stage3(redirect_trampoline* self, cpu_flags flags)
        {
            auto f = self->func;
            redirect_allocator alloc { &*trampoline_memres };
            std::allocator_traits<redirect_allocator>::destroy(alloc, self);
            std::allocator_traits<redirect_allocator>::deallocate(alloc, self, 1);
            asm ("push %0; popf" : : "rm" (flags) : "cc");
            f();
        }
    };

    void kill()
    {
        jw::terminate();
    }

    template <exception_num N, exception_num... Next>
    static void make_throwers()
    {
        exception_throwers[N].emplace(N, [](const exception_info& i) -> bool
        {
            if constexpr (config::enable_throwing_from_cpu_exceptions)
                throw specific_cpu_exception<N> { i };
            return false;
        });
        if constexpr (sizeof...(Next) > 0) make_throwers<Next...>();
    }

    void setup_exception_handling()
    {
        constinit static bool done { false };
        if (done) return;
        done = true;

        trampoline_memres.emplace(trampoline_pool);
        exception_data.ds = get_ds();
        exception_data.stack_end = 0xDEADBEEF;

        pending_exceptions.emplace();

        make_throwers<0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e>();

        try { make_throwers<0x10, 0x11, 0x12, 0x13, 0x14, 0x1e>(); }
        catch (const dpmi_error&) { /* ignore */ }
    }
}

namespace jw::dpmi
{
    void redirect_exception(exception_frame* frame, void(*func)())
    {
        if (frame->info_bits.redirect_elsewhere) throw already_redirected { };

        using namespace ::jw::dpmi::detail;
        redirect_allocator alloc { &*trampoline_memres };
        auto* const p = std::allocator_traits<redirect_allocator>::allocate(alloc, 1);
        std::allocator_traits<redirect_allocator>::construct(alloc, p, std::uintptr_t { frame->fault_address.offset }, cpu_flags { frame->flags }, func);

        frame->flags.interrupts_enabled = false;
        frame->fault_address.offset = p->code();
        frame->info_bits.redirect_elsewhere = true;
    }

    std::string cpu_category::message(int ev) const
    {
        using namespace std::string_literals;
        switch (ev)
        {
        case exception_num::divide_error:             return "Divide error"s;
        case exception_num::trap:                     return "Debug exception"s;
        case exception_num::non_maskable_interrupt:   return "Non-maskable interrupt"s;
        case exception_num::breakpoint:               return "Breakpoint"s;
        case exception_num::overflow:                 return "Overflow"s;
        case exception_num::bound_range_exceeded:     return "Bound range exceeded"s;
        case exception_num::invalid_opcode:           return "Invalid opcode"s;
        case exception_num::device_not_available:     return "Device not available"s;
        case exception_num::double_fault:             return "Double fault"s;
        case exception_num::x87_segment_not_present:  return "x87 Segment overrun"s;
        case exception_num::invalid_tss:              return "Invalid Task State Segment"s;
        case exception_num::segment_not_present:      return "Segment not present"s;
        case exception_num::stack_segment_fault:      return "Stack Segment fault"s;
        case exception_num::general_protection_fault: return "General protection fault"s;
        case exception_num::page_fault:               return "Page fault"s;
        case exception_num::x87_exception:            return "x87 Floating-point exception"s;
        case exception_num::alignment_check:          return "Alignment check"s;
        case exception_num::machine_check:            return "Machine check"s;
        case exception_num::sse_exception:            return "SSE Floating-point exception"s;
        case exception_num::virtualization_exception: return "Virtualization exception"s;
        case exception_num::security_exception:       return "Security exception"s;
        default: return fmt::format("Unknown CPU exception {:0>#2x}", ev);
        }
    }
}
