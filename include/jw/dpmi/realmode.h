/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/dpmi/memory.h>
#include <jw/dpmi/irq.h>
#include <function.h>

namespace jw
{
    namespace dpmi
    {
        // CPU register structure for DPMI real-mode functions.
        struct alignas(2) [[gnu::packed]] realmode_registers : public cpu_registers
        {
            union[[gnu::packed]]
            {
                std::uint16_t raw_flags;
                struct[[gnu::packed]]
                {
                    bool carry : 1;
                    unsigned : 1;
                    bool parity : 1;
                    unsigned : 1;
                    bool adjust : 1;
                    unsigned : 1;
                    bool zero : 1;
                    bool sign : 1;
                    bool trap : 1;
                    bool interrupt : 1;
                    bool direction : 1;
                    bool overflow : 1;
                    unsigned iopl : 2;
                    bool nested_task : 1;
                    unsigned : 1;
                } flags;
            };
            std::uint16_t es, ds, fs, gs;
            std::uint16_t ip, cs; // not used in call_int()
            std::uint16_t sp, ss; // not required for call_int()

            auto& print(std::ostream& out) const
            {
                using namespace std;
                out << hex << setfill('0');
                out << "es=" << setw(4) << es << " ds=" << setw(4) << ds << " fs=" << setw(4) << fs << " gs=" << setw(4) << gs << "\n";
                out << "cs=" << setw(4) << cs << " ip=" << setw(4) << ip << " ss=" << setw(4) << ss << " sp=" << setw(4) << sp << " flags=" << setw(4) << raw_flags << "\n";
                out << hex << setfill(' ') << setw(0) << flush;
                return out;
            }
            friend auto& operator<<(std::ostream& out, const realmode_registers& in) { return in.print(out); }

            // Call a real-mode interrupt
            void call_int(int_vector interrupt) { call(interrupt, 0x0300); }

            // Call a real-mode procedure that returns with RETF
            // Function address given by CS:IP fields
            void call_far() { call(0, 0x0301); }

            // Call a real-mode procedure that returns with IRET
            // Function address given by CS:IP fields
            void call_far_iret() { call(0, 0x0302); }

            // Call a real-mode procedure that returns with RETF
            void call_far(far_ptr16 ptr) { ip = ptr.offset; cs = ptr.segment; call_far(); }

            // Call a real-mode procedure that returns with IRET
            void call_far_iret(far_ptr16 ptr) { ip = ptr.offset; cs = ptr.segment; call_far_iret(); }

        private:
            void call(int_vector interrupt, std::uint16_t dpmi_function)
            {
                selector new_reg_ds = get_ds();
                realmode_registers* new_reg;
                dpmi_error_code error;
                bool c;

                asm volatile(
                    "push es;"
                    "mov es, %w2;"
                    "int 0x31;"
                    "mov %w2, es;"
                    "pop es;"
                    : "=@ccc" (c)
                    , "=a" (error)
                    , "+rm" (new_reg_ds)
                    , "=D" (new_reg)
                    : "a" (dpmi_function)
                    , "b" (interrupt)
                    , "D" (this)
                    , "c" (0)   // TODO: stack?
                    : "memory");
                if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
                copy_from(new_reg_ds, new_reg);
            }

            void copy_from(selector new_reg_ds, realmode_registers* new_reg)
            {
                if (__builtin_expect(new_reg == this && new_reg_ds == get_ds(), true)) return;
                auto ptr = linear_memory { new_reg_ds, new_reg };
                if (__builtin_expect(ptr.requires_new_selector(), false))
                {
                    asm("push es;"
                        "push ds;"
                        "pop es;"
                        "mov ds, %w0;"
                        "rep movsb;"
                        "push es;"
                        "pop ds;"
                        "pop es;"
                        :: "rm" (new_reg_ds)
                        , "c" (sizeof(realmode_registers))
                        , "S" (new_reg)
                        , "D" (this)
                        : "memory");
                }
                else *this = *(ptr.get_ptr<realmode_registers>());
            }
        };

        static_assert(sizeof(realmode_registers) == 0x32, "check sizeof struct dpmi::realmode_registers");

        // Reference for writing real-mode callback functions:
        // http://www.delorie.com/djgpp/doc/dpmi/ch4.6.html

        struct realmode_callback_base
        {
            virtual ~realmode_callback_base() { free(); }

            realmode_callback_base(const realmode_callback_base&) = delete;
            realmode_callback_base(realmode_callback_base&&) = delete;
            realmode_callback_base& operator=(const realmode_callback_base&) = delete;
            realmode_callback_base& operator=(realmode_callback_base&&) = delete;

            far_ptr16 get_ptr() const noexcept { return ptr; }

        protected:
            realmode_callback_base(auto* function_ptr) { alloc(reinterpret_cast<void*>(function_ptr)); }
            realmode_registers reg;

        private:
            far_ptr16 ptr;
            void alloc(void* function_ptr);
            void free();
        };

        struct realmode_callback : public realmode_callback_base, class_lock<realmode_callback>
        {
            template<typename F>
            realmode_callback(F&& function, std::size_t pool_size = 1_KB) 
                : realmode_callback_base(code.data())
                , function_ptr(std::allocator_arg, locking_allocator<> { }, std::forward<F>(function))
                , alloc(pool_size), reg_pool(alloc) { init_code(); }

        private:
            static void entry_point(realmode_callback* self, std::uint32_t rm_stack_selector, std::uint32_t rm_stack_offset) noexcept;
            void init_code() noexcept;

            func::function<void(realmode_registers*, far_ptr32)> function_ptr;
            std::array<byte, 16_KB> stack;  // TODO: adjustable size
            locked_pool_allocator<> alloc;
            std::vector<realmode_registers, locked_pool_allocator<>> reg_pool;

            [[gnu::packed]] selector fs;                                            // [eax-0x19]
            [[gnu::packed]] selector gs;                                            // [eax-0x17]
            [[gnu::packed]] decltype(&entry_point) entry_ptr { &entry_point };      // [eax-0x15]
            [[gnu::packed]] byte* stack_ptr { stack.data() + stack.size() - 4 };    // [eax-0x11]
            [[gnu::packed]] realmode_registers* reg_ptr;                            // [eax-0x0D]
            [[gnu::packed]] realmode_callback* self { this };                       // [eax-0x09]
            std::array<byte, 0x60> code;                                            // [eax-0x05]
        };
    }
}
