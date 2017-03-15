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

                static far_ptr32 get_rm_handler(exception_num n) { return get_rm_exception_handler(n); }

                static bool set_handler(exception_num n, far_ptr32 ptr, bool pm_only = false)
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
                if (c) throw dpmi_error(error, __FUNCTION__);       \
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
                if (c) throw dpmi_error(error, __FUNCTION__);


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
        }
    }
}
