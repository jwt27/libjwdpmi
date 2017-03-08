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

// Hardware exception handling functionality.

#pragma once

#include <iostream>
#include <cstdint>
#include <algorithm>
#include <deque>
#include <jw/dpmi/dpmi.h>
#include <jw/dpmi/lock.h>
#include <jw/dpmi/alloc.h>
#include <jw/dpmi/irq_check.h>
#include <jw/dpmi/fpu.h>
#include <../jwdpmi_config.h>

namespace jw
{
    namespace dpmi
    {
        struct [[gnu::packed]] old_exception_frame
        {
            far_ptr32 return_address; unsigned : 16;
            std::uint32_t error_code;
            far_ptr32 fault_address;
            struct [[gnu::packed]] // DPMI 1.0 only
            {
                bool host_exception : 1;
                bool cannot_retry : 1;
                bool redirect_elsewhere : 1;
                unsigned : 13;
            } info_bits;
            struct [[gnu::packed]]
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
                bool resume : 1;
                bool v86mode : 1;
                bool alignment_check : 1;
                bool virtual_interrupt : 1;
                bool virtual_interrupt_pending : 1;
                bool cpuid_available : 1;
                unsigned : 10;
            } flags;
            far_ptr32 stack; unsigned : 16;
        };
        struct [[gnu::packed]] new_exception_frame : public old_exception_frame
        {
            selector es; unsigned : 16;
            selector ds; unsigned : 16;
            selector fs; unsigned : 16;
            selector gs;
            std::uintptr_t linear_page_fault_address : 32;
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
        using exception_num = std::uint32_t;


        struct cpu_exception
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

            static far_ptr32 get_rm_handler(exception_num n) { return get_rm_exception_handler(n); }

            static bool set_handler(exception_num n, far_ptr32 ptr)// , bool pm_only = true)
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
                }/*
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
                }*/
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

            static void set_exception_handler(exception_num n, far_ptr32 handler) { CALL_INT31_SET(0x0203, n, handler); }       //DPMI 0.9 AX=0203
            static void set_pm_exception_handler(exception_num n, far_ptr32 handler) { CALL_INT31_SET(0x0212, n, handler); }    //DPMI 1.0 AX=0212
            static void set_rm_exception_handler(exception_num n, far_ptr32 handler) { CALL_INT31_SET(0x0213, n, handler); }    //DPMI 1.0 AX=0213

        #undef CALL_INT31_SET
        #undef CALL_INT31_GET
        };

        class exception_handler : class_lock<exception_handler>
        {
            using handler_type = bool(exception_frame* frame, bool is_new_type);

            std::function<handler_type> handler;
            exception_num exc;
            static std::array<byte, config::exception_stack_size> stack; // TODO: allow nested exceptions
            static std::array<std::unique_ptr<std::deque<exception_handler*>>, 0x20> wrapper_list;

            static bool call_handler(exception_handler* self, raw_exception_frame* frame) noexcept;
                                                                // sizeof   alignof     offset
            exception_handler* self { this };                   // 4        4           [eax-0x28]
            decltype(&call_handler) call_ptr { &call_handler }; // 4        4           [eax-0x24]
            byte* stack_ptr;                                    // 4        4           [eax-0x20]
            selector ds;                                        // 2        2           [eax-0x1C]
            selector es;                                        // 2        2           [eax-0x1A]
            selector fs;                                        // 2        2           [eax-0x18]
            selector gs;                                        // 2        2           [eax-0x16]
            bool new_type;                                      // 1        1           [eax-0x14]
            byte _padding;                                      // 1        1           [eax-0x13]
            far_ptr32 previous_handler;                         // 6        1           [eax-0x12]
            std::array<byte, 0x100> code;                       //          1           [eax-0x0C]

        public:
            exception_handler(exception_num e, std::function<handler_type> f);

            ~exception_handler();

            exception_handler(const exception_handler&) = delete;
            exception_handler(exception_handler&&) = delete;           
            exception_handler& operator=(const exception_handler&) = delete;
            exception_handler& operator=(exception_handler&&) = delete;

            far_ptr32 get_ptr() const noexcept { return far_ptr32 { get_cs(), reinterpret_cast<std::uintptr_t>(code.data()) }; }
        };
    }
}
