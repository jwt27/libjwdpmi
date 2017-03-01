// Hardware exception handling functionality.

#pragma once

#include <iostream>
#include <cstdint>
#include <algorithm>
#include <jw/dpmi/dpmi.h>
#include <jw/dpmi/irq.h>
#include <jw/dpmi/lock.h>
#include <jw/dpmi/alloc.h>

namespace jw
{
    namespace dpmi
    {
        struct [[gnu::packed]] old_exception_frame
        {
            far_ptr32 return_address; unsigned : 16;
            unsigned error_code;
            far_ptr32 address;
            struct [[gnu::packed]] // DPMI 1.0 only
            {
                bool host_exception : 1;
                bool cannot_retry : 1;
                bool redirect_elsewhere : 1;
                unsigned : 13;
            } info_bits;
            unsigned flags; // TODO: struct
            far_ptr32 stack; unsigned : 16;
        };
        struct [[gnu::packed]] new_exception_frame : public old_exception_frame
        {
            selector es; unsigned : 16;
            selector ds; unsigned : 16;
            selector fs; unsigned : 16;
            selector gs;
            unsigned linear_page_fault_address : 32;
            struct [[gnu::packed]]
            {
                bool present : 1;
                bool write_access : 1;
                bool user_access : 1;
                bool write_through : 1;
                bool cache_disabled : 1;
                bool accessed : 1;
                bool dirty : 1;
                bool global : 1;
                unsigned reserved : 3;
                unsigned physical_address : 21;
            } page_table_entry;
        };

        struct [[gnu::packed]] raw_exception_frame
        {
            old_exception_frame frame_09;
            new_exception_frame frame_10;
        };

        using exception_frame = old_exception_frame; // can be static_cast to new_exception_frame type
        using exception_handler_sig = bool(exception_frame*, bool);
        using exception_num = unsigned;


        class cpu_exception
        {
        public:

            static far_ptr32 get_pm_handler(exception_num n)
            {
                try { return get_pm_exception_handler(n); }
                catch (dpmi_error e)
                {
                    switch (e.code().value())
                    {
                    case dpmi_error_code::unsupported_function:
                    case 0x0210:
                        return get_exception_handler(n);
                    default:
                        throw e;
                    }
                }
            }

            static far_ptr32 get_rm_handler(exception_num n) { return get_rm_exception_handler(n); }

            template<typename T>
            static bool set_handler(exception_num n, T* handler, bool pm_only = false)
            {
                bool is_new_type { true };
                far_ptr32 ptr { get_cs(), reinterpret_cast<std::uintptr_t>(handler) };

                try { set_pm_exception_handler(n, ptr); }
                catch (dpmi_error& e)
                {
                    switch (e.code().value())
                    {
                    case dpmi_error_code::unsupported_function:
                    case 0x0212:
                        set_exception_handler(n, ptr);
                        is_new_type = false;
                        break;
                    default:
                        throw e;
                    }
                }
                if (!pm_only && is_new_type)
                {
                    try { set_rm_exception_handler(n, ptr); }
                    catch (dpmi_error& e)
                    {
                        switch (e.code().value())
                        {
                        case dpmi_error_code::unsupported_function:
                        case 0x0213:
                            // too bad.
                            break;
                        default:
                            throw e;
                        }
                    }
                }
                return is_new_type;
            }


        private:

        #define CALL_INT31_GET(func_no, exc_no)                     \
                dpmi_error_code error;                              \
                selector seg;                                       \
                std::size_t offset;                                 \
                asm volatile                                        \
                    ("get_exc_handler_"#func_no"_%=:"               \
                     "mov eax, "#func_no";"                         \
                     "int 0x31;"                                    \
                     "jc get_exc_handler_"#func_no"_end_%=;"        \
                     "mov eax, 0;"                                  \
                     "get_exc_handler_"#func_no"_end_%=:"           \
                     : "=a" (error)                                 \
                     , "=c" (seg)                                   \
                     , "=d" (offset)                                \
                     : "b" (exc_no)                                 \
                     : "cc");                                       \
                if (error) throw dpmi_error(error, __FUNCTION__);   \
                return far_ptr32(seg, offset);

        #define CALL_INT31_SET(func_no, exc_no, handler_ptr)        \
                dpmi_error_code error;                              \
                asm volatile                                        \
                    ("set_exc_handler_"#func_no"_%=:"               \
                     "mov eax, "#func_no";"                         \
                     "int 0x31;"                                    \
                     "jc set_exc_handler_"#func_no"_end%=;"         \
                     "mov eax, 0;"                                  \
                     "set_exc_handler_"#func_no"_end%=:;"           \
                     : "=a" (error)                                 \
                     : "b" (exc_no)                                 \
                     , "c" (handler_ptr.segment)                    \
                     , "d" (handler_ptr.offset)                     \
                     : "cc");                                       \
                if (error) throw dpmi_error(error, __FUNCTION__);


            static far_ptr32 get_exception_handler(exception_num n) { CALL_INT31_GET(0x0202, n); }      //DPMI 0.9 AX=0202
            static far_ptr32 get_pm_exception_handler(exception_num n) { CALL_INT31_GET(0x0210, n); }   //DPMI 1.0 AX=0210
            static far_ptr32 get_rm_exception_handler(exception_num n) { CALL_INT31_GET(0x0211, n); }   //DPMI 1.0 AX=0211

            static void set_exception_handler(exception_num n, far_ptr32 handler) { CALL_INT31_SET(0x0203, n, handler); }        //DPMI 0.9 AX=0203
            static void set_pm_exception_handler(exception_num n, far_ptr32 handler) { CALL_INT31_SET(0x0212, n, handler); }    //DPMI 1.0 AX=0212
            static void set_rm_exception_handler(exception_num n, far_ptr32 handler) { CALL_INT31_SET(0x0213, n, handler); }    //DPMI 1.0 AX=0213

        #undef CALL_INT31_SET
        #undef CALL_INT31_GET
        };

        class locked_stack : class_lock<locked_stack>
        {
            alignas(0x10) std::array<byte, 32_KB> stack;

        public:
            auto* get_ptr() { return stack.data() + stack.size() - 4; }
            auto size() { return stack.size(); }
        };


        class exception_wrapper : class_lock<exception_wrapper>
        {
            using handler_type = bool(exception_frame* frame, bool is_new_type);

            std::function<handler_type> handler;
            locked_stack stack;

            static bool call_handler(exception_wrapper* self, raw_exception_frame* frame)
            {
                auto* f = self->new_type ? &frame->frame_10 : &frame->frame_09;
                return self->handler(f, self->new_type);
            }
                                                                // sizeof   alignof     offset
            exception_wrapper* self { this };                   // 4        4           [eax-0x28]
            decltype(&call_handler) call_ptr { &call_handler }; // 4        4           [eax-0x24]
            byte* stack_ptr { stack.get_ptr() };                // 4        4           [eax-0x20]
            selector ds;                                        // 2        2           [eax-0x1C]
            selector es;                                        // 2        2           [eax-0x1A]
            selector fs;                                        // 2        2           [eax-0x18]
            selector gs;                                        // 2        2           [eax-0x16]
            bool new_type;                                      // 1        1           [eax-0x14]
            byte _padding;                                      // 1        1           [eax-0x13]
            far_ptr32 previous_handler;                         // 6        1           [eax-0x12]
            std::array<byte, 0x100> code;                       //          1           [eax-0x0C]

        public:
            template<typename F>
            exception_wrapper(exception_num e, F f) : handler(f)
            {
                byte* start;
                std::size_t size;
                asm volatile (
                    "jmp exception_wrapper_end%=;"
                    // --- \/\/\/\/\/\/ --- //
                    "exception_wrapper_begin%=:;"

                    "pushd ds; pushd es; pushd fs; pushd gs; pushad;"   // 7 bytes
                    "call get_eip%=;"                                   // 5 bytes
                    "get_eip%=: pop eax;"   // Get EIP and use it to find our variables

                    // Copy exception frame to the new stack
                    "mov ebp, esp;"
                    "mov es, cs:[eax-0x1C];"    // note: this is DS
                    "push ss; pop ds;"
                    "mov ecx, 0x16;"            // sizeof raw_exception_frame = 0x58 bytes (0x16 dwords)
                    "lea esi, [ebp+0x30];"
                    "mov edi, cs:[eax-0x20];"   // new stack pointer
                    "sub edi, ecx;"
                    "and edi, -0x10;"           // align stack to 0x10 bytes
                    "mov ebx, edi;"
                    "cld;"
                    "rep movsd;"

                    // Restore segment registers
                    "mov ds, cs:[eax-0x1C];"
                    "mov es, cs:[eax-0x1A];"
                    "mov fs, cs:[eax-0x18];"
                    "mov gs, cs:[eax-0x16];"

                    // Switch to the new stack
                    "mov cx, ss;"
                    "push ds; pop ss;"
                    "mov esp, ebx;"
                    "push ecx;"

                    "push ebx;"                 // Pointer to raw_exception_frame
                    "push cs:[eax-0x28];"       // Pointer to self
                    "mov ebx, eax;"
                    "xor eax, eax;"
                    "call cs:[ebx-0x24];"       // call_handler();
                    "add esp, 8;"

                    // Copy exception frame to host stack
                    "popd es;"
                    "mov ecx, 0x16;"
                    "mov esi, esp;"
                    "lea edi, [ebp+0x30];"
                    "cld;"
                    "rep movsd;"

                    // Switch back to host stack
                    "push es; pop ss;"
                    "mov esp, ebp;"

                    "test al, al;"                  // Check return value
                    "jz chain%=;"                   // Chain if false
                    "mov al, cs:[ebx-0x14];"
                    "test al, al;"                  // Check which frame to return
                    "jz old_type%=;"

                    // Return with DPMI 1.0 frame
                    "popad; popd gs; popd fs; popd es; popd ds;"
                    "add esp, 0x20;"
                    "retf;"

                    // Return with DPMI 0.9 frame
                    "old_type%=:;"
                    "popad; popd gs; popd fs; popd es; popd ds;"
                    "retf;"

                    // Chain to previous handler
                    "chain%=:"
                    "push cs; pop ds;"
                    "push ss; pop es;"
                    "lea esi, [ebx-0x12];"          // previous_handler
                    "lea edi, [esp-0x06];"
                    "movsd; movsw;"                 // copy previous_handler ptr to stack
                    "popad; popd gs; popd fs; popd es; popd ds;"
                    "jmp fword ptr ss:[esp-0x36];"

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

                asm volatile (
                    "mov %w0, ds;"
                    "mov %w0, es;"
                    "mov %w0, fs;"
                    "mov %w0, gs;"
                    : "=m" (ds)
                    , "=m" (es)
                    , "=m" (fs)
                    , "=m" (gs));

                previous_handler = cpu_exception::get_pm_handler(e);
                new_type = cpu_exception::set_handler(e, code.data(), true);
            }

            ~exception_wrapper()
            {
                // TODO: restore old handler...
            }
        };

        class simple_exception_wrapper : class_lock<simple_exception_wrapper>
        {
            using handler_type = void();
                                            // sizeof   alignof     offset
            handler_type* function_ptr;     // 4        4           [esi-0x10]
            far_ptr32 previous_handler;     // 6        1           [esi-0x0C]
            std::array<byte, 0x100> code;   //          1           [esi-0x06]

        public:
            template <typename F>
            simple_exception_wrapper(exception_num e, F f) : function_ptr(f)
            {
                byte* start;
                std::size_t size;
                asm volatile (
                    "jmp simple_exception_handler_end%=;"
                    // --- \/\/\/\/\/\/ --- //
                    "simple_exception_handler_begin%=:;"

                    "pushad;"               // 1 byte
                    "call get_eip%=;"       // 5 bytes
                    "get_eip%=: pop esi;"

                    "lea ebp, [esp+0x20];"
                    "mov ax, cs;"
                    "cmp ax, ss:[ebp+0x10];"                // CS
                    "jne chain%=;"                          // chain if not our CS

                    "push ds;"
                    "mov ds, ss:[ebp+0x1C];"                // SS
                    "sub dword ptr ss:[ebp+0x18], 0x4;"     // ESP
                    "mov edi, ss:[ebp+0x18];"
                    "mov eax, ss:[ebp+0x0C];"               // EIP
                    "mov ds:[edi], eax;"

                    "mov eax, dword ptr cs:[esi-0x10];"     // function_ptr
                    "mov ss:[ebp+0x0C], eax;"               // EIP

                    "pop ds;"
                    "popad;"
                    "retf;"

                    // Chain to previous handler
                    "chain%=:"
                    "push cs; pop ds;"
                    "push ss; pop es;"
                    "lea esi, [esi-0x0C];"          // previous_handler
                    "lea edi, [esp-0x06];"
                    "movsd; movsw;"                 // copy previous_handler ptr to stack
                    "popad;"
                    "jmp fword ptr ss:[esp-0x26];"

                    "simple_exception_handler_end%=:;"
                    // --- /\/\/\/\/\/\ --- //
                    "mov %0, offset simple_exception_handler_begin%=;"
                    "mov %1, offset simple_exception_handler_end%=;"
                    "sub %1, %0;"                   // size = end - begin
                    : "=r,r,m" (start)
                    , "=r,m,r" (size)
                    ::"cc");
                assert(size <= code.size());

                auto* ptr = memory_descriptor(get_cs(), start, size).get_ptr<byte>();
                std::copy_n(ptr, size, code.data());

                previous_handler = cpu_exception::get_pm_handler(e);
                cpu_exception::set_handler(e, code.data(), true);
                std::cout << "exception handler installed" << std::endl;
                std::cout << "code at " << std::hex << get_cs() << ":" << (std::uintptr_t)code.data() << std::endl;
            }

            ~simple_exception_wrapper()
            {
                // TODO: restore old handler...
            }
        };
    }
}
