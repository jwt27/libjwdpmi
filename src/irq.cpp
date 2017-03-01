#include <algorithm>
#include <jw/dpmi/irq.h>

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            std::atomic<std::uint32_t> interrupt_count { 0 };
            locked_pool_allocator<> irq::alloc { 1_MB }; // TODO: sensible value, or configurable.
            std::vector<int_vector, locked_pool_allocator<>> irq::current_int { alloc };
            std::unordered_map<int_vector, irq, std::hash<int_vector>, std::equal_to<int_vector>, locking_allocator<>> irq::entries { };
            static std::vector<std::array<byte, config::interrupt_stack_size>, locking_allocator<>> stacks;

            void irq::interrupt_entry_point(int_vector vec) noexcept
            {
                ++interrupt_count;
                current_int.push_back(vec);
                // TODO: save/restore FPU state?

                std::unique_ptr<irq_mask> mask;
                if (!(entries.at(vec).flags & no_reentry_at_all)) asm("sti");
                else if (entries.at(vec).flags & no_reentry) mask = std::make_unique<irq_mask>(vec_to_irq(vec)); // TODO: vec -> irq
                //if (!(entries.at(vec).flags & no_auto_eoi)) acknowledge();

                // TODO maybe: do stack switch here
                interrupt_call_handler(vec);

                acknowledge();
                {
                    interrupt_mask no_ints_here { };
                    --interrupt_count;
                    current_int.pop_back();
                }
            }

            void irq::interrupt_call_handler(int_vector vec) noexcept
            {
                try
                {
                    entries.at(vec)();
                }
                catch (...) { std::cerr << "OOPS" << std::endl; } // TODO: exception handling
            }

            irq_wrapper::irq_wrapper(int_vector _vec, function_ptr f) noexcept : vec(_vec), entry_point(f)
            {
                byte* start;
                std::size_t size;
                asm volatile (
                    "jmp interrupt_wrapper_end%=;"
                    // --- \/\/\/\/\/\/ --- //
                    "interrupt_wrapper_begin%=:;"   // On entry, the only known register is CS.

                    "push ds; push es; push fs; push gs; pusha;"    // 7 bytes
                    "call get_eip%=;"  // call near/relative (E8)   // 5 bytes
                    "get_eip%=: pop eax;"           // Pop EIP into EAX and use it to find our vars
                    "mov ds, cs:[eax-0x18];"        // Restore segment registers
                    "mov es, cs:[eax-0x16];"
                    "mov fs, cs:[eax-0x14];"
                    "mov gs, cs:[eax-0x12];"
                    "push cs:[eax-0x1C];"           // Pass our interrupt vector along
                    "call cs:[eax-0x10];"           // Call the entry point
                    "add esp, 4;"
                    "popa; pop gs; pop fs; pop es; pop ds;"
                    "sti;"                          // IRET is not guaranteed to set the interrupt flag.
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
                    : "=m" (ds)
                    , "=m" (es)
                    , "=m" (fs)
                    , "=m" (gs));
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
