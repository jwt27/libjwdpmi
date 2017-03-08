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

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            volatile std::uint32_t exception_count { 0 };
        }

        std::array<std::unique_ptr<std::deque<exception_handler*>>, 0x20> exception_handler::wrapper_list;
        std::array<byte, config::exception_stack_size> exception_handler::stack;

        bool exception_handler::call_handler(exception_handler * self, raw_exception_frame * frame) noexcept
        {
            //std::clog << "entering exc handler " << self->exc << '\n';
            ++detail::exception_count;
            if (self->exc != 0x07 && self->exc != 0x10) detail::fpu_context_switcher.enter();
            bool success = false;
            try
            {
                auto* f = self->new_type ? &frame->frame_10 : &frame->frame_09;
                success = self->handler(f, self->new_type);
            }
            catch (...)
            {
                std::cerr << "CAUGHT EXCEPTION IN CPU EXCEPTION HANDLER " << self->exc << std::endl; // HACK
            }                                                                                        // ... but what else can you do here?
            if (self->exc != 0x07 && self->exc != 0x10) detail::fpu_context_switcher.leave();
            --detail::exception_count;
            //std::clog << "leaving exc handler " << self->exc << '\n';
            return success;
        }

        exception_handler::exception_handler(exception_num e, std::function<handler_type> f) : handler(f), exc(e), stack_ptr(stack.data() + stack.size())
        {
            byte* start;
            std::size_t size;
            asm volatile (
                "jmp exception_wrapper_end%=;"
                // --- \/\/\/\/\/\/ --- //
                "exception_wrapper_begin%=:;"

                "push ds; push es; push fs; push gs; pusha;"    // 7 bytes
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
                "sub edi, ecx;"
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

                "sub esp, 4;"
                "add ebp, 0x30;"
                "push ebp;"                 // Pointer to raw_exception_frame
                "push cs:[eax-0x28];"       // Pointer to self
                "mov ebx, eax;"
                "call cs:[ebx-0x24];"       // call_handler();
                "add esp, 0xC;"
                "test al, al;"              // Check return value
                "jz chain%=;"               // Chain if false
                "mov al, cs:[ebx-0x14];"
                "test al, al;"              // Check which frame to return
                "jz old_type%=;"

                // Return with DPMI 1.0 frame
                "popa; pop gs; pop fs; pop es; pop ds;"
                "add esp, 0x20;"
                "retf;"

                // Return with DPMI 0.9 frame
                "old_type%=:;"
                "popa; pop gs; pop fs; pop es; pop ds;"
                "retf;"

                // Chain to previous handler
                "chain%=:"
                "push cs; pop ds;"
                "push ss; pop es;"
                "lea esi, [ebx-0x12];"      // previous_handler
                "lea edi, [esp-0x06];"
                "movsd; movsw;"             // copy previous_handler ptr above stack (is this dangerous?)
                "popa; pop gs; pop fs; pop es; pop ds;"
                "jmp fword ptr ss:[esp-0x2A];"

                "exception_wrapper_end%=:;"
                // --- /\/\/\/\/\/\ --- //
                "mov %0, offset exception_wrapper_begin%=;"
                "mov %1, offset exception_wrapper_end%=;"
                "sub %1, %0;"
                : "=r,r,m" (start)
                , "=r,m,r" (size)
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

            if (!wrapper_list[e]) wrapper_list[e] = std::make_unique<std::deque<exception_handler*>>();

            if (wrapper_list[e]->empty()) previous_handler = cpu_exception::get_pm_handler(e);
            else previous_handler = wrapper_list[e]->back()->get_ptr();
            wrapper_list[e]->push_back(this);

            new_type = cpu_exception::set_handler(e, get_ptr());
        }

        exception_handler::~exception_handler() // TODO: find out why it's crashing here sometimes.
        {
            if (!wrapper_list[exc]) return;
            auto i = std::find(wrapper_list[exc]->begin(), wrapper_list[exc]->end(), this);
            if (wrapper_list[exc]->back() == this) cpu_exception::set_handler(exc, previous_handler);
            else if (i[+1] != nullptr) i[+1]->previous_handler = previous_handler;
            wrapper_list[exc]->erase(i);
        }
    }
}

