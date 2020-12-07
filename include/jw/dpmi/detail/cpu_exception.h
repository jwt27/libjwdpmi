/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once

namespace jw::dpmi::detail
{
    struct cpu_exception_handlers
    {
        static far_ptr32 get_pm_handler(exception_num n)
        {
            try { return int31_get<0x0210>(n); }
            catch (const dpmi_error& e)
            {
                switch (e.code().value())
                {
                case dpmi_error_code::unsupported_function:
                case 0x0210:
                    return int31_get<0x0202>(n);
                default:
                    throw;
                }
            }
        }

        static bool set_pm_handler(exception_num n, const far_ptr32& ptr)
        {
            try { int31_set<0x0212>(n, ptr); }
            catch (const dpmi_error& e)
            {
                switch (e.code().value())
                {
                case dpmi_error_code::unsupported_function:
                case 0x0212:
                    int31_set<0x0203>(n, ptr);
                    return false;
                default:
                    throw;
                }
            }
            return true;
        }

        static far_ptr32 get_rm_handler(exception_num n) { return int31_get<0x0211>(n); }

        static void set_rm_handler(exception_num n, const far_ptr32& ptr) { int31_set<0x0213>(n, ptr); }

    private:
        cpu_exception_handlers() = delete;

        template <std::uint16_t Func>
        static far_ptr32 int31_get(exception_num exc_no)
        {
            dpmi_error_code error;
            bool c;
            selector seg;
            std::uintptr_t offset;
            asm
            (
                "int 0x31;"
                : "=@ccc" (c)
                , "=a" (error)
                , "=c" (seg)
                , "=d" (offset)
                : "a" (Func)
                , "b" (exc_no)
                : "cc"
            );
            if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
            return { seg, offset };
        }

        template <std::uint16_t Func>
        static void int31_set(exception_num exc_no, const far_ptr32& handler_ptr)
        {
            dpmi_error_code error;
            bool c;
            asm volatile
            (
                "int 0x31;"
                : "=@ccc" (c)
                , "=a" (error)
                : "a" (Func)
                , "b" (exc_no)
                , "c" (handler_ptr.segment)
                , "d" (handler_ptr.offset)
                : "cc"
            );
            if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
        }
    };

    void setup_exception_throwers();

    [[noreturn, gnu::no_caller_saved_registers, gnu::force_align_arg_pointer]]
    void kill();

    void simulate_call(exception_frame*, void(*)()) noexcept;
}
