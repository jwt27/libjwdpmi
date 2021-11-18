/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
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
            auto result = int31_get<0x0210>(n);
            if (auto* error = std::get_if<dpmi_error_code>(&result)) [[unlikely]]
            {
                switch (static_cast<unsigned>(*error))
                {
                case dpmi_error_code::unsupported_function:
                case 0x0210:
                    result = int31_get<0x0202>(n);
                    if (result.index() == 0) [[likely]] break;
                    error = std::get_if<dpmi_error_code>(&result);
                    [[fallthrough]];
                default:
                    throw dpmi_error(*error, __PRETTY_FUNCTION__);
                }
            }
            return *std::get_if<far_ptr32>(&result);
        }

        static bool set_pm_handler(exception_num n, const far_ptr32& ptr)
        {
            auto error = int31_set<0x0212>(n, ptr);
            if (not error) [[likely]] return true;
            switch (static_cast<unsigned>(*error))
            {
            case static_cast<dpmi_error_code>(0x0212):
            case dpmi_error_code::unsupported_function:
                error = int31_set<0x0203>(n, ptr);
                if (not error) [[likely]] return false;
                [[fallthrough]];
            default:
                throw dpmi_error(*error, __PRETTY_FUNCTION__);
            }
        }

        static far_ptr32 get_rm_handler(exception_num n)
        {
            auto result = int31_get<0x0211>(n);
            if (auto* error = std::get_if<dpmi_error_code>(&result)) [[unlikely]]
                throw dpmi_error(*error, __PRETTY_FUNCTION__);
            return *std::get_if<far_ptr32>(&result);
        }

        static void set_rm_handler(exception_num n, const far_ptr32& ptr)
        {
            auto error = int31_set<0x0213>(n, ptr);
            if (not error) [[likely]] return;
            throw dpmi_error(*error, __PRETTY_FUNCTION__);
        }

    private:
        cpu_exception_handlers() = delete;

        template <std::uint16_t Func>
        static std::variant<far_ptr32, dpmi_error_code> int31_get(exception_num exc_no)
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
            if (c) [[unlikely]] return { error };
            return far_ptr32 { seg, offset };
        }

        template <std::uint16_t Func>
        static std::optional<dpmi_error_code> int31_set(exception_num exc_no, const far_ptr32& handler_ptr)
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
            if (c) [[unlikely]] return { error };
            return std::nullopt;
        }
    };

    void setup_exception_throwers();

    [[noreturn, gnu::no_caller_saved_registers, gnu::force_align_arg_pointer]]
    void kill();

    void simulate_call(exception_frame*, void(*)()) noexcept;
}
