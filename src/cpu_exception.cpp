/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#include <jw/dpmi/cpu_exception.h>
#include <jw/debug/debug.h>
#include <jw/dpmi/detail/interrupt_id.h>
#include <jw/dpmi/ring0.h>
#include <cstring>
#include <vector>

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            std::vector<std::exception_ptr> pending_exceptions { };

            [[noreturn, gnu::no_caller_saved_registers, gnu::force_align_arg_pointer]]
            void kill()
            {
                jw::terminate();
            }

            [[noreturn, gnu::no_caller_saved_registers, gnu::force_align_arg_pointer]]
            void rethrow_cpu_exception()
            {
                auto e = pending_exceptions.back();
                pending_exceptions.pop_back();
                std::rethrow_exception(e);
            }

            [[gnu::naked, gnu::stdcall]]
            void call_from_exception(void(*)())
            {
                asm (".cfi_signal_frame");
                asm ("call [esp+4]; ret 4");
            }

            bool enter_exception_context(exception_num exc) noexcept
            {
                ++detail::exception_count;
                auto fpu_context_switch_handled = detail::fpu_context_switcher->enter(exc);
                detail::interrupt_id::push_back(exc, detail::interrupt_id::id_t::exception);
                return fpu_context_switch_handled;
            }

            void leave_exception_context() noexcept
            {
                detail::interrupt_id::pop_back();
                detail::fpu_context_switcher->leave();
                --detail::exception_count;
            }
        }

        bool exception_handler::call_handler(exception_handler* self, raw_exception_frame* frame) noexcept
        {
            if (detail::enter_exception_context(self->exc))
            {   // fpu context switch
                detail::leave_exception_context();
                return true;
            }
            *reinterpret_cast<volatile std::uint32_t*>(stack.begin()) = 0xDEADBEEF;
            bool success = false;
            auto* f = self->new_type ? &frame->frame_10 : &frame->frame_09;
            try
            {
                success = self->handler(&frame->reg, f, self->new_type);
            }
            catch (...)
            {
                auto really_throw = [&]
                {
                    if constexpr (not config::enable_throwing_from_cpu_exceptions) return false;    // Only throw if this option is enabled
                    if (f->fault_address.segment != get_cs()
                        and f->fault_address.segment != detail::ring0_cs) return false;             // and exception happened in our code
                    if (f->flags.v86_mode) return false;                                            // and not in real mode (sanity check)
                    return true;
                };

                if (really_throw())
                {
                    detail::pending_exceptions.emplace_back(std::current_exception());
                    detail::simulate_call(f, detail::rethrow_cpu_exception);
                    success = true;
                }
                else
                {
                    std::cerr << "CAUGHT EXCEPTION IN CPU EXCEPTION HANDLER 0x" << std::hex << self->exc << std::endl;
                    try { std::rethrow_exception(std::current_exception()); }
                    catch (const std::exception& e) { print_exception(e); }
                    catch (...) { }
                    detail::simulate_call(f, detail::kill);
                }
            }
            if (*reinterpret_cast<volatile std::uint32_t*>(stack.begin()) != 0xDEADBEEF) std::cerr << "STACK OVERFLOW\n"; // another HACK
            detail::leave_exception_context();
            return success;
        }

        void exception_handler::init_code()
        {
            byte* start;
            std::size_t size;
            asm volatile (
                "jmp exception_wrapper_end%=;"
                // --- \/\/\/\/\/\/ --- //
                "exception_wrapper_begin%=:"

                "pusha; push ds; push es; push fs; push gs;"    // 7 bytes
                "call Lget_eip;"                                // 5 bytes
                "Lget_eip: pop eax;"        // Get EIP and use it to find our variables

                "mov ebp, esp;"
                "mov edx, esp;"
                "mov bx, ss;"
                "cmp bx, word ptr cs:[eax-0x1C];"
                "je Lkeep_stack;"

                // Copy frame to new stack
                "les edi, cs:[eax-0x20];"   // new stack pointer
                "sub edi, 0x88;"            // exception_frame = 0x58 bytes, pushed regs = 0x30 bytes, total 0x88 bytes
                "mov ecx, 0x22;"
                "push ss; pop ds;"
                "mov esi, ebp;"
                "mov ebp, edi;"
                "cld;"
                "rep movsd;"

                // Switch to the new stack
                "mov ss, cs:[eax-0x1C];"
                "mov esp, ebp;"
                "Lkeep_stack:"
                "push ebx;"                 // previous SS
                "push edx;"                 // previous ESP

                // Restore segment registers
                "mov ds, cs:[eax-0x1C];"
                "mov es, cs:[eax-0x1A];"
                "mov fs, cs:[eax-0x18];"
                "mov gs, cs:[eax-0x16];"

                "lea edx, [ebp+0x10];"
                "push edx;"                 // Pointer to raw_exception_frame
                "push cs:[eax-0x28];"       // Pointer to self
                "mov ebx, eax;"
                "call cs:[ebx-0x24];"       // call_handler();
                "cli;"
                "add esp, 0x08;"

                // Copy frame and switch back to previous stack
                "mov dx, ss;"
                "cmp dx, [esp+0x04];"
                "je Lret_same_stack;"
                "mov esi, ebp;"
                "les edi, [esp];"
                "mov ecx, 0x22;"
                "rep movsd;"
                "lss esp, [esp];"
                "mov ebp, esp;"
                "Lret_same_stack:"
                "mov esp, ebp;"

                "test al, al;"              // Check return value
                "jz Lchain;"                // Chain if false
                "mov al, cs:[ebx-0x14];"
                "test al, al;"              // Check which frame to return
                "jz Lold_type;"

                // Return with DPMI 1.0 frame
                "pop gs; pop fs; pop es; pop ds; popa;"
                "add esp, 0x20;"
                "retf;"

                // Return with DPMI 0.9 frame
                "Lold_type:"
                "pop gs; pop fs; pop es; pop ds; popa;"
                "retf;"

                // Chain to previous handler
                "Lchain:"
                "mov eax, cs:[ebx-0x12];"   // copy chain_to ptr above stack (is this dangerous?)
                "mov ss:[esp-0x08], eax;"
                "mov ax, cs:[ebx-0x0e];"
                "mov ss:[esp-0x04], ax;"
                "pop gs; pop fs; pop es; pop ds; popa;"
                "jmp fword ptr ss:[esp-0x38];"

                "exception_wrapper_end%=:"
                // --- /\/\/\/\/\/\ --- //
                "mov %0, offset exception_wrapper_begin%=;"
                "mov %1, offset exception_wrapper_end%= - exception_wrapper_begin%=;"
                : "=rm" (start)
                , "=rm" (size)
                ::"cc");
            assert(size <= code.size());

            auto* ptr = linear_memory(get_cs(), start, size).get_ptr<byte>();
            std::copy_n(ptr, size, code.data());
            auto cs_limit = reinterpret_cast<std::size_t>(code.data() + size);
            if (descriptor::get_limit(get_cs()) < cs_limit)
                descriptor::set_limit(get_cs(), cs_limit);

            asm volatile(
                "mov %w0, ds;"
                "mov %w1, es;"
                "mov %w2, fs;"
                "mov %w3, gs;"
                : "=m" (ds)
                , "=m" (es)
                , "=m" (fs)
                , "=m" (gs));

            ++detail::interrupt_id::get()->use_count;
        }

        exception_handler::~exception_handler()
        {
            if (next != nullptr)    // middle of chain
            {
                next->prev = prev;
                next->chain_to = chain_to;
            }
            else                    // last in chain
            {
                last[exc] = prev;
                detail::cpu_exception_handlers::set_pm_handler(exc, chain_to);
            }
            --detail::interrupt_id::get()->use_count;
            detail::interrupt_id::delete_if_possible();
        }

        std::string cpu_category::message(int ev) const
        {
            switch (ev)
            {
            case exception_num::divide_error:             return "Divide error.";
            case exception_num::trap:                     return "Debug exception.";
            case exception_num::non_maskable_interrupt:   return "Non-maskable interrupt.";
            case exception_num::breakpoint:               return "Breakpoint.";
            case exception_num::overflow:                 return "Overflow.";
            case exception_num::bound_range_exceeded:     return "Bound range exceeded.";
            case exception_num::invalid_opcode:           return "Invalid opcode.";
            case exception_num::device_not_available:     return "Device not available.";
            case exception_num::double_fault:             return "Double fault.";
            case exception_num::x87_segment_not_present:  return "x87 Segment overrun.";
            case exception_num::invalid_tss:              return "Invalid Task State Segment.";
            case exception_num::segment_not_present:      return "Segment not present.";
            case exception_num::stack_segment_fault:      return "Stack Segment fault.";
            case exception_num::general_protection_fault: return "General Protection fault.";
            case exception_num::page_fault:               return "Page fault.";
            case exception_num::x87_exception:            return "x87 Floating-point exception.";
            case exception_num::alignment_check:          return "Alignment check.";
            case exception_num::machine_check:            return "Machine check.";
            case exception_num::sse_exception:            return "SSE Floating-point exception.";
            case exception_num::virtualization_exception: return "Virtualization exception.";
            case exception_num::security_exception:       return "Security exception.";
            default:std::stringstream s; s << "Unknown CPU exception 0x" << std::hex << ev << "."; return s.str();
            }
        }

        namespace detail
        {
            std::array<std::optional<exception_handler>, 0x20> exception_throwers;

            template <exception_num N>
            void make_thrower()
            {
                exception_throwers[N].emplace(N, [](cpu_registers* r, exception_frame* f, bool t) -> bool
                {
                    throw specific_cpu_exception<N> { r, f, t };
                });
            }

            template <exception_num N, exception_num... Next>
            void make_throwers()
            {
                make_thrower<N>();
                if constexpr (sizeof...(Next) > 0) make_throwers<Next...>();
            };

            bool exception_throwers_setup { false };
            void setup_exception_throwers()
            {
                if constexpr (not config::enable_throwing_from_cpu_exceptions) return;
                if (exception_throwers_setup) [[likely]] return;
                exception_throwers_setup = true;

                make_throwers<0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                              0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e>();

                capabilities c { };
                if (not c.supported) return;
                if (std::strncmp(c.vendor_info.name, "HDPMI", 5) != 0) return;  // TODO: figure out if other hosts support these too
                make_throwers<0x10, 0x11, 0x12, 0x13, 0x14, 0x1e>();
            }
        }
    }
}
