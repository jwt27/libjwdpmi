/******************************* libjwdpmi **********************************
Copyright (C) 2016-2017  J.W. Jagersma

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <jw/dpmi/cpu_exception.h>
#include <jw/dpmi/debug.h>
#include <cstring>

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            volatile std::uint32_t exception_count { 0 };
        }

        std::array<exception_handler*, 0x20> exception_handler::last { };
        std::array<byte, config::exception_stack_size> exception_handler::stack;

        bool exception_handler::call_handler(exception_handler * self, raw_exception_frame * frame) noexcept
        {
            ++detail::exception_count;
            if (self->exc != exception_num::device_not_available) detail::fpu_context_switcher.enter();
            *reinterpret_cast<volatile std::uint32_t*>(stack.begin()) = 0xDEADBEEF;
            bool success = false;
            try
            {
                auto* f = self->new_type ? &frame->frame_10 : &frame->frame_09;
                success = self->handler(&frame->reg, f, self->new_type);
            }
            catch (...)
            {
                std::cerr << "CAUGHT EXCEPTION IN CPU EXCEPTION HANDLER " << self->exc << std::endl; // HACK
            }                                                                                        // ... but what else can you do here?
            if (*reinterpret_cast<volatile std::uint32_t*>(stack.begin()) != 0xDEADBEEF) std::cerr << "STACK OVERFLOW\n"; // another HACK
            if (self->exc != exception_num::device_not_available) detail::fpu_context_switcher.leave();
            --detail::exception_count;
            return success;
        }
        
        void exception_handler::init_code()
        {
            byte* start;
            std::size_t size;
            asm volatile (
                "jmp exception_wrapper_end%=;"
                // --- \/\/\/\/\/\/ --- //
                "exception_wrapper_begin%=:;"

                "pusha; push ds; push es; push fs; push gs;"    // 7 bytes
                "call get_eip%=;"                               // 5 bytes
                "get_eip%=: pop eax;"       // Get EIP and use it to find our variables

                "mov ebp, esp;"
                "lea edi, [ebp-0x20];"
                "mov bx, ss;"
                "cmp bx, word ptr cs:[eax-0x1C];"
                "je keep_stack%=;"
                "mov edi, cs:[eax-0x20];"   // new stack pointer
                "keep_stack%=:"
                "mov ecx, 0x22;"            // exception_frame = 0x58 bytes, pushed regs = 0x30 bytes, total 0x22 dwords
                "sub edi, 0x88;"
                "and edi, -0x10;"           // align stack
                "mov es, cs:[eax-0x1C];"    // note: this is DS
                "push ss; pop ds;"
                "mov esi, ebp;"
                "mov ebp, edi;"
                "cld;"
                "rep movsd;"

                // Restore segment registers
                "mov ds, cs:[eax-0x1C];"
                "mov es, cs:[eax-0x1A];"
                "mov fs, cs:[eax-0x18];"
                "mov gs, cs:[eax-0x16];"

                // Switch to the new stack
                "mov ss, cs:[eax-0x1C];"
                "mov esp, ebp;"

                "push ebx;"
                "add ebp, 0x10;"
                "push ebp;"                 // Pointer to raw_exception_frame
                "push cs:[eax-0x28];"       // Pointer to self
                "mov ebx, eax;"
                "call cs:[ebx-0x24];"       // call_handler();
                "cli;"
                "add esp, 0x8;"
                "pop edx;"

                /*
                "mov es, dx;"
                "mov ecx, 0x22;"
                "mov edi, esi;"
                "sub edi, 0x88;"
                "mov esi, esp;"
                "mov ebp, edi;"
                "cld;"
                "rep movsd;"

                "mov ss, dx;"
                "mov esp, ebp;"
                */

                "test al, al;"              // Check return value
                "jz chain%=;"               // Chain if false
                "mov al, cs:[ebx-0x14];"
                "test al, al;"              // Check which frame to return
                "jz old_type%=;"

                // Return with DPMI 1.0 frame
                "pop gs; pop fs; pop es; pop ds; popa;"
                "add esp, 0x20;"
                "retf;"

                // Return with DPMI 0.9 frame
                "old_type%=:;"
                "pop gs; pop fs; pop es; pop ds; popa;"
                "retf;"

                // Chain to previous handler
                "chain%=:"
                "mov eax, cs:[ebx-0x12];"   // copy chain_to ptr above stack (is this dangerous?)
                "mov ss:[esp-0x08], eax;"
                "mov ax, cs:[ebx-0x0e];"
                "mov ss:[esp-0x04], ax;"
                "pop gs; pop fs; pop es; pop ds; popa;"
                "jmp fword ptr ss:[esp-0x38];"

                "exception_wrapper_end%=:;"
                // --- /\/\/\/\/\/\ --- //
                "mov %0, offset exception_wrapper_begin%=;"
                "mov %1, offset exception_wrapper_end%=;"
                "sub %1, %0;"
                : "=rm,r" (start)
                , "=r,rm" (size)
                ::"cc");
            assert(size <= code.size());

            auto* ptr = memory_descriptor(get_cs(), start, size).get_ptr<byte>();
            std::copy_n(ptr, size, code.data());

            asm volatile(
                "mov %w0, ds;"
                "mov %w1, es;"
                "mov %w2, fs;"
                "mov %w3, gs;"
                : "=m" (ds)
                , "=m" (es)
                , "=m" (fs)
                , "=m" (gs));
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
        }

        std::string cpu_category::message(int ev) const
        {
            switch (ev)
            {
            case exception_num::divide_error:             return "Divide error.";
            case exception_num::debug:                    return "Debug exception.";
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
            new_exception_frame last_exception_frame;
            cpu_registers last_exception_registers;
            bool throwing_exception { false };

            std::string create_exception_message()
            {
                std::stringstream s;
                s << last_exception_frame;
                s << last_exception_registers;
                return s.str();
            }

            [[noreturn, gnu::used, gnu::optimize("no-omit-frame-pointer")]] 
            void throw_cpu_exception(std::uint32_t n) 
            {
                throwing_exception = false;
                throw cpu_exception(n, create_exception_message());
            }

            bool simulate_call(std::uint32_t exc, cpu_registers *reg, exception_frame* frame, bool new_type, auto* func) noexcept
            {
                if (frame->fault_address.segment != get_cs()) return false;     // Only throw if exception happened in our code
                if (frame->flags.v86mode) return false;                         // and not in real mode
                if (new_type && frame->info_bits.host_exception) return false;  // and not in the DPMI host
                if (throwing_exception) return false;                           // and we're not recursing
                throwing_exception = true;

                last_exception_frame = { };
                if (new_type) last_exception_frame = *static_cast<new_exception_frame*>(frame);
                else static_cast<old_exception_frame&>(last_exception_frame) = *frame;
                last_exception_registers = *reg;

                frame->stack.offset -= 8;                                                               // "sub esp, 8"
                frame->stack.offset &= -0x10;                                                           // "and esp, -0x10"
                *reinterpret_cast<std::uint32_t*>(frame->stack.offset + 4) = exc;                       // "mov [esp+4], exc"
                *reinterpret_cast<std::uintptr_t*>(frame->stack.offset) = frame->fault_address.offset;  // "mov [esp], eip"
                frame->fault_address.offset = reinterpret_cast<std::uintptr_t>(func);                   // "mov eip, func"
                frame->info_bits.redirect_elsewhere = true;
                return true;
            }

            std::array<std::unique_ptr<exception_handler>, 0x20> exception_throwers;

            bool exception_throwers_setup { false };
            void setup_exception_throwers()
            {
                if (!config::enable_throwing_from_cpu_exceptions) return;
                if (exception_throwers_setup) return;
                exception_throwers_setup = true;

                exception_throwers[0x00] = std::make_unique<exception_handler>(0x00, [](cpu_registers* r, exception_frame* f, bool t) { return simulate_call(0x00, r, f, t, throw_cpu_exception); });
                exception_throwers[0x01] = std::make_unique<exception_handler>(0x01, [](cpu_registers* r, exception_frame* f, bool t) { return simulate_call(0x01, r, f, t, throw_cpu_exception); });
                exception_throwers[0x02] = std::make_unique<exception_handler>(0x02, [](cpu_registers* r, exception_frame* f, bool t) { return simulate_call(0x02, r, f, t, throw_cpu_exception); });
                exception_throwers[0x03] = std::make_unique<exception_handler>(0x03, [](cpu_registers* r, exception_frame* f, bool t) { return simulate_call(0x03, r, f, t, throw_cpu_exception); });
                exception_throwers[0x04] = std::make_unique<exception_handler>(0x04, [](cpu_registers* r, exception_frame* f, bool t) { return simulate_call(0x04, r, f, t, throw_cpu_exception); });
                exception_throwers[0x05] = std::make_unique<exception_handler>(0x05, [](cpu_registers* r, exception_frame* f, bool t) { return simulate_call(0x05, r, f, t, throw_cpu_exception); });
                exception_throwers[0x06] = std::make_unique<exception_handler>(0x06, [](cpu_registers* r, exception_frame* f, bool t) { return simulate_call(0x06, r, f, t, throw_cpu_exception); });
                exception_throwers[0x07] = std::make_unique<exception_handler>(0x07, [](cpu_registers* r, exception_frame* f, bool t) { return simulate_call(0x07, r, f, t, throw_cpu_exception); });
                exception_throwers[0x08] = std::make_unique<exception_handler>(0x08, [](cpu_registers* r, exception_frame* f, bool t) { return simulate_call(0x08, r, f, t, throw_cpu_exception); });
                exception_throwers[0x09] = std::make_unique<exception_handler>(0x09, [](cpu_registers* r, exception_frame* f, bool t) { return simulate_call(0x09, r, f, t, throw_cpu_exception); });
                exception_throwers[0x0a] = std::make_unique<exception_handler>(0x0a, [](cpu_registers* r, exception_frame* f, bool t) { return simulate_call(0x0a, r, f, t, throw_cpu_exception); });
                exception_throwers[0x0b] = std::make_unique<exception_handler>(0x0b, [](cpu_registers* r, exception_frame* f, bool t) { return simulate_call(0x0b, r, f, t, throw_cpu_exception); });
                exception_throwers[0x0c] = std::make_unique<exception_handler>(0x0c, [](cpu_registers* r, exception_frame* f, bool t) { return simulate_call(0x0c, r, f, t, throw_cpu_exception); });
                exception_throwers[0x0d] = std::make_unique<exception_handler>(0x0d, [](cpu_registers* r, exception_frame* f, bool t) { return simulate_call(0x0d, r, f, t, throw_cpu_exception); });
                exception_throwers[0x0e] = std::make_unique<exception_handler>(0x0e, [](cpu_registers* r, exception_frame* f, bool t) { return simulate_call(0x0e, r, f, t, throw_cpu_exception); });

                capabilities c { };
                if (!c.supported) return;
                if (std::strncmp(c.vendor_info.name, "HDPMI", 5) != 0) return;  // TODO: figure out if other hosts support these too
                exception_throwers[0x10] = std::make_unique<exception_handler>(0x10, [](cpu_registers* r, exception_frame* f, bool t) { return simulate_call(0x10, r, f, t, throw_cpu_exception); });
                exception_throwers[0x11] = std::make_unique<exception_handler>(0x11, [](cpu_registers* r, exception_frame* f, bool t) { return simulate_call(0x11, r, f, t, throw_cpu_exception); });
                exception_throwers[0x12] = std::make_unique<exception_handler>(0x12, [](cpu_registers* r, exception_frame* f, bool t) { return simulate_call(0x12, r, f, t, throw_cpu_exception); });
                exception_throwers[0x13] = std::make_unique<exception_handler>(0x13, [](cpu_registers* r, exception_frame* f, bool t) { return simulate_call(0x13, r, f, t, throw_cpu_exception); });
                exception_throwers[0x14] = std::make_unique<exception_handler>(0x14, [](cpu_registers* r, exception_frame* f, bool t) { return simulate_call(0x14, r, f, t, throw_cpu_exception); });
                exception_throwers[0x1e] = std::make_unique<exception_handler>(0x1e, [](cpu_registers* r, exception_frame* f, bool t) { return simulate_call(0x1e, r, f, t, throw_cpu_exception); });
            }
        }
    }
}

