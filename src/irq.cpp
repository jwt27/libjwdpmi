#include <algorithm>
#include <jw/dpmi/irq.h>

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            volatile std::uint32_t interrupt_count { 0 };
            locked_pool_allocator<> irq::alloc { 1_MB }; // TODO: sensible value, or configurable.
            std::vector<int_vector, locked_pool_allocator<>> irq::current_int { alloc };
            std::unordered_map<int_vector, irq, std::hash<int_vector>, std::equal_to<int_vector>, locking_allocator<>> irq::entries { };
            std::vector<std::array<byte, config::interrupt_stack_size>, locking_allocator<>> irq::stacks { };
            std::uint32_t irq::max_interrupt_count { 0 };
            irq::initializer irq::init { };

            void irq::interrupt_entry_point(int_vector vec) noexcept
            {
                current_int.push_back(vec);                                                                      
                max_interrupt_count = std::max(max_interrupt_count, static_cast<std::uint32_t>(interrupt_count));
                if (is_irq(vec) && !in_service()[vec_to_irq(vec)]) goto spurious;

                // TODO: save/restore FPU state?
                try
                {
                    std::shared_ptr<irq_mask> mask;
                    if (!(entries.at(vec).flags & no_reentry_at_all)) asm("sti");
                    else if (entries.at(vec).flags & no_reentry) mask = std::allocate_shared<irq_mask>(alloc, vec_to_irq(vec));
                    if (!(entries.at(vec).flags & no_auto_eoi)) send_eoi();
                
                    entries.at(vec)();
                }
                catch (...) { std::cerr << "OOPS" << std::endl; } // TODO: exception handling

                spurious:
                asm("cli");
                acknowledge();
                --interrupt_count;
                current_int.pop_back();
            }

            void irq::operator()()
            {
                for (auto f : handler_chain)
                {
                    try
                    {
                        if (f->flags & always_call || !is_acknowledged()) f->handler_ptr(acknowledge);
                    }
                    catch (...) { std::cerr << "EXCEPTION OCCURED IN INTERRUPT HANDLER " << std::hex << vec << std::endl; } // TODO: exceptions
                }
                if (flags & always_chain || !is_acknowledged()) call_far_iret(old_handler);
            }

            irq_wrapper::irq_wrapper(int_vector _vec, entry_fptr entry_f, stack_fptr stack_f) noexcept : get_stack(stack_f), vec(_vec), entry_point(entry_f)
            {
                byte* start;
                std::size_t size;
                asm volatile (
                    "jmp interrupt_wrapper_end%=;"
                    // --- \/\/\/\/\/\/ --- //
                    "interrupt_wrapper_begin%=:;"   // On entry, the only known register is CS.

                    "push ds; push es; push fs; push gs; pusha;"    // 7 bytes
                    "call get_eip%=;"  // call near/relative (E8)   // 5 bytes
                    "get_eip%=: pop esi;"
                    "mov ds, cs:[esi-0x18];"        // Restore segment registers
                    "mov es, cs:[esi-0x16];"
                    "mov fs, cs:[esi-0x14];"
                    "mov gs, cs:[esi-0x12];"
                    "call cs:[esi-0x20];"           // Get a stack pointer
                    "and eax, -0x10;"               // Align stack
                    "mov ebp, esp;"
                    "mov bx, ss;"
                    "mov ss, cs:[esi-0x22];"
                    "mov esp, eax;"
                    "push bx;"
                    "push cs:[esi-0x1C];"           // Pass our interrupt vector
                    "call cs:[esi-0x10];"           // Call the entry point
                    "add esp, 4;"
                    "pop ss;"
                    "mov esp, ebp;"
                    "popa; pop gs; pop fs; pop es; pop ds;"
                    "sti;"
                    "iret;"

                    "interrupt_wrapper_end%=:;"
                    // --- /\/\/\/\/\/\ --- //
                    "mov %0, offset interrupt_wrapper_begin%=;"
                    "mov %1, offset interrupt_wrapper_end%=;"
                    "sub %1, %0;"                   // size = end - begin
                    : "=rm,r" (start)
                    , "=r,rm" (size)
                    ::"cc");
                assert(size <= code.size());

                auto* ptr = memory_descriptor(get_cs(), start, size).get_ptr<byte>();
                std::copy_n(ptr, size, code.data());

                asm volatile (
                    "mov %w0, ds;"
                    "mov %w1, es;"
                    "mov %w2, fs;"
                    "mov %w3, gs;"
                    "mov %w4, ss;"
                    : "=m" (ds)
                    , "=m" (es)
                    , "=m" (fs)
                    , "=m" (gs)
                    , "=m" (ss));
            }

            void irq::set_pm_interrupt_vector(int_vector v, far_ptr32 ptr)
            {
                dpmi_error_code error;
                asm volatile
                    ("int 0x31;"
                     "jc fail%=;"
                     "mov eax, 0;"
                     "fail%=:;"
                     : "=a" (error)
                     : "a" (0x0205)
                     , "b" (v)
                     , "c" (ptr.segment)
                     , "d" (ptr.offset)
                     : "cc");
                if (error) throw dpmi_error(error, __FUNCTION__);
            }

            far_ptr32 irq::get_pm_interrupt_vector(int_vector v)
            {
                dpmi_error_code error;
                far_ptr32 ptr;
                asm volatile
                    ("int 0x31;"
                     "jc fail%=;"
                     "mov eax, 0;"
                     "fail%=:;"
                     : "=a" (error)
                     , "=c" (ptr.segment)
                     , "=d" (ptr.offset)
                     : "a" (0x0204)
                     , "b" (v)
                     : "cc");
                if (error) throw dpmi_error(error, __FUNCTION__);
                return ptr;
            }
        }
    }
}
