/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
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

    struct exception_trampoline;

    struct exception_handler_data
    {
        function<exception_handler_sig> func;
        const exception_num num;
        exception_trampoline* next { nullptr };
        exception_trampoline* prev;
        bool is_dpmi10;
        const bool realmode;

    private:
        friend struct exception_trampoline;

        template <typename F>
        exception_handler_data(exception_num n, F&& f, bool rm)
            : func { std::forward<F>(f) }
            , num { n }
            , realmode { rm } { }
    };

    struct exception_trampoline
    {
        template<typename F>
        static exception_trampoline* create(exception_num n, F&& f, bool rm)
        {
            auto* const p = allocate();
            return new (p) exception_trampoline { n, std::forward<F>(f), rm };
        }

        static void destroy(exception_trampoline* p)
        {
            p->~exception_trampoline();
            deallocate(p);
        }

        bool is_dpmi10() const noexcept
        {
            return data->is_dpmi10;
        }

    private:
        static inline constinit std::array<exception_trampoline*, 0x1f> last { };
        static inline constinit locking_allocator<exception_handler_data> data_alloc { };

        static exception_trampoline* allocate();
        static void deallocate(exception_trampoline* p);
        std::ptrdiff_t find_entry_point(bool) const noexcept;

        template<typename F>
        exception_trampoline(exception_num n, F&& f, bool rm)
            : data { data_alloc.allocate(1) }
        {
            data = new (data) exception_handler_data { n, std::forward<F>(f), rm };
            data->prev = last[n];
            if (data->prev != nullptr) data->prev->data->next = this;
            last[n] = this;

            auto chain_to = detail::cpu_exception_handlers::get_pm_handler(n);
            chain_to_segment = chain_to.segment;
            chain_to_offset = chain_to.offset;

            interrupt_mask no_irqs { };
            const auto p = reinterpret_cast<std::uintptr_t>(&push0_imm32);
            if (rm)
            {
                data->is_dpmi10 = true;
                detail::cpu_exception_handlers::set_rm_handler(n, { get_cs(), p });
            }
            else data->is_dpmi10 = detail::cpu_exception_handlers::set_pm_handler(n, { get_cs(), p });
            entry_point = find_entry_point(data->is_dpmi10);
        }

        ~exception_trampoline();

        exception_trampoline(exception_trampoline&&) = delete;
        exception_trampoline(const exception_trampoline&) = delete;
        exception_trampoline& operator=(exception_trampoline&&) = delete;
        exception_trampoline& operator=(const exception_trampoline&) = delete;

        struct alignas(0x10) [[gnu::packed]]
        {
            const std::uint8_t push0_imm32 { 0x68 };
            selector chain_to_segment;
            const unsigned : 16;
            const std::uint8_t push1_imm32 { 0x68 };
            std::uintptr_t chain_to_offset;
            const std::uint8_t push2_imm32 { 0x68 };
            exception_handler_data* data;
            const std::uint8_t jmp_rel32 { 0xe9 };
            std::ptrdiff_t entry_point;
        };
    };

    struct raw_exception_frame
    {
        selector gs;
        unsigned : 16;
        selector fs;
        unsigned : 16;
        selector es;
        unsigned : 16;
        selector ds;
        unsigned : 16;
        cpu_registers reg;
        const exception_handler_data* data;
        far_ptr32 chain_to;
        unsigned : 16;
        dpmi09_exception_frame frame_09;
        dpmi10_exception_frame frame_10;
    };

    static_assert(sizeof(raw_exception_frame) == 0x94);

    void setup_exception_handling();

    [[noreturn]]
    void kill();
}
