/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            struct cpu_exception_handlers
            {
                static far_ptr32 get_pm_handler(exception_num n)
                {
                    try { return get_pm_exception_handler(n); }
                    catch (const dpmi_error& e)
                    {
                        switch (e.code().value())
                        {
                        case dpmi_error_code::unsupported_function:
                        case 0x0210:
                            return get_exception_handler(n);
                        default:
                            throw;
                        }
                    }
                }

                static bool set_pm_handler(exception_num n, far_ptr32 ptr)
                {
                    static bool is_new_type { true };

                    try { set_pm_exception_handler(n, ptr); }
                    catch (const dpmi_error& e)
                    {
                        switch (e.code().value())
                        {
                        case dpmi_error_code::unsupported_function:
                        case 0x0212:
                            set_exception_handler(n, ptr);
                            is_new_type = false;
                            break;
                        default:
                            throw;
                        }
                    }
                    return is_new_type;
                }

                static far_ptr32 get_rm_handler(exception_num n) { return get_rm_exception_handler(n); }

                static void set_rm_handler(exception_num n, far_ptr32 ptr) { set_rm_exception_handler(n, ptr); }

            private:
            #define CALL_INT31_GET(func_no, exc_no)                 \
                dpmi_error_code error;                              \
                bool c;                                             \
                selector seg;                                       \
                std::size_t offset;                                 \
                asm("int 0x31;"                                     \
                    : "=@ccc" (c)                                   \
                    , "=a" (error)                                  \
                    , "=c" (seg)                                    \
                    , "=d" (offset)                                 \
                    : "a" (func_no)                                 \
                    , "b" (exc_no)                                  \
                    : "cc");                                        \
                if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);\
                return far_ptr32(seg, offset);

            #define CALL_INT31_SET(func_no, exc_no, handler_ptr)    \
                dpmi_error_code error;                              \
                bool c;                                             \
                asm volatile(                                       \
                    "int 0x31;"                                     \
                    : "=@ccc" (c)                                   \
                    , "=a" (error)                                  \
                    : "a" (func_no)                                 \
                    , "b" (exc_no)                                  \
                    , "c" (handler_ptr.segment)                     \
                    , "d" (handler_ptr.offset)                      \
                    : "cc");                                        \
                if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);


                static far_ptr32 get_exception_handler(exception_num n) { CALL_INT31_GET(0x0202, n); }      //DPMI 0.9 AX=0202
                static far_ptr32 get_pm_exception_handler(exception_num n) { CALL_INT31_GET(0x0210, n); }   //DPMI 1.0 AX=0210
                static far_ptr32 get_rm_exception_handler(exception_num n) { CALL_INT31_GET(0x0211, n); }   //DPMI 1.0 AX=0211

                static void set_exception_handler(exception_num n, far_ptr32 handler) { CALL_INT31_SET(0x0203, n, handler); }       //DPMI 0.9 AX=0203
                static void set_pm_exception_handler(exception_num n, far_ptr32 handler) { CALL_INT31_SET(0x0212, n, handler); }    //DPMI 1.0 AX=0212
                static void set_rm_exception_handler(exception_num n, far_ptr32 handler) { CALL_INT31_SET(0x0213, n, handler); }    //DPMI 1.0 AX=0213

            #undef CALL_INT31_SET
            #undef CALL_INT31_GET
            };

            void setup_exception_throwers();

            [[noreturn, gnu::no_caller_saved_registers]] void kill();

            inline void simulate_call(exception_frame* frame, void(*func)()) noexcept
            {
                frame->stack.offset -= 4;                                                               // "sub esp, 4"
                *reinterpret_cast<std::uintptr_t*>(frame->stack.offset) = frame->fault_address.offset;  // "mov [esp], eip"
                frame->fault_address.offset = reinterpret_cast<std::uintptr_t>(func);                   // "mov eip, func"
                frame->info_bits.redirect_elsewhere = true;
            }
        }
    }
}
