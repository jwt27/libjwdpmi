/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2016 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <jw/dpmi/dpmi_error.h>
#include <jw/split_int.h>
#include <jw/common.h>
#include <fmt/core.h>
#include <memory>
#include <array>

namespace jw
{
    namespace dpmi
    {
        using selector = std::uint16_t;

    #define GET_SEG_REG(reg)                \
        std::uint32_t s;                    \
        asm ("mov %0, "#reg : "=r" (s));    \
        [[assume((s & 0xffffu) == s)]];     \
        return s;

        inline selector get_cs() noexcept { GET_SEG_REG(cs); }
        inline selector get_ds() noexcept { GET_SEG_REG(ds); }
        inline selector get_ss() noexcept { GET_SEG_REG(ss); }
        inline selector get_es() noexcept { GET_SEG_REG(es); }
        inline selector get_fs() noexcept { GET_SEG_REG(fs); }
        inline selector get_gs() noexcept { GET_SEG_REG(gs); }
    #undef GET_SEG_REG

        struct version
        {
            std::uint8_t major, minor;

            struct
            {
                bool host_is_32bit : 1;
                bool reflect_int_to_real_mode : 1;
                bool supports_virtual_memory : 1;
                bool : 5, : 8;
            } flags;

            enum : std::uint8_t
            {
                cpu_i286 = 2,
                cpu_i386 = 3,
                cpu_i486 = 4,
                cpu_i586 = 5,
                cpu_i686 = 6
            } cpu_type;

            std::uint8_t pic_master_base, pic_slave_base;

            version() noexcept;
        };

        // Get optional DPMI 1.0 capabilities of current DPMI host
        struct capabilities
        {
            struct
            {
                bool page_dirty : 1;
                bool exceptions_restartability : 1;
                bool device_mapping : 1;
                bool conventional_memory_mapping : 1;
                bool demand_zero_fill : 1;
                bool write_protect_client : 1;
                bool write_protect_host : 1;
                bool : 1, : 8;
            } flags;

            struct
            {
                struct
                {
                    std::uint8_t major;
                    std::uint8_t minor;
                } version;
                std::array<char, 126> name;
            } vendor_info;

            static std::optional<capabilities> get() noexcept;

        private:
            constexpr capabilities() noexcept = default;
        };

        struct alignas(2) [[gnu::packed]] far_ptr16
        {
            std::uint16_t offset, segment;

            constexpr far_ptr16(selector seg = 0, std::uint16_t off = 0) noexcept
                : offset { off }
                , segment { seg }
            { }
        };

        struct alignas(2) [[gnu::packed]] far_ptr32
        {
            std::uintptr_t offset;
            selector segment;

            constexpr far_ptr32(selector seg = 0, std::uintptr_t off = 0) noexcept
                : offset { off }
                , segment { seg }
            { }
        };

        struct gs_override
        {
            gs_override(selector new_gs) { set_gs(new_gs); }
            ~gs_override() { set_gs(prev_gs); }

            gs_override() = delete;
            gs_override(const gs_override&) = delete;
            gs_override(gs_override&&) = delete;
            gs_override& operator=(const gs_override&) = delete;
            gs_override& operator=(gs_override&&) = delete;

        private:
            void set_gs(std::uint32_t s) { asm volatile("mov gs, %0;" :: "r" (s)); }
            const selector prev_gs { get_gs() };
        };

        // Call a function which returns with RETF
        inline void call_far(far_ptr32 ptr)
        {
            force_frame_pointer();
            asm volatile(
                "pusha;"
                "call fword ptr %0;"
                "popa;"
                :: "m" (ptr)
                : "memory");
        }

        // Call a function which returns with IRET
        inline void call_far_iret(far_ptr32 ptr)
        {
            force_frame_pointer();
            asm volatile(
                "pusha;"
                "pushf;"
                "call fword ptr %0;"
                "popa;"
                :: "m" (ptr)
                : "memory");
        }

        // EFLAGS register
        struct cpu_flags
        {
            bool carry : 1;
            bool : 1;
            bool parity : 1;
            bool : 1;
            bool adjust : 1;
            bool : 1;
            bool zero : 1;
            bool sign : 1;
            bool trap : 1;
            bool interrupts_enabled : 1;
            bool direction : 1;
            bool overflow : 1;
            unsigned io_privilege : 2;
            bool nested_task : 1;
            bool : 1;
            bool resume : 1;
            bool v86_mode : 1;
            bool alignment_check : 1;
            bool virtual_interrupts_enabled : 1;
            bool virtual_interrupts_pending : 1;
            bool cpuid : 1;
            unsigned : 10;

            void get() { asm volatile ("pushfd; pop %0" : "=rm" (*this)); }
            static cpu_flags current() { cpu_flags f; f.get(); return f; }
        };
        static_assert (sizeof(cpu_flags) == 4);

        // All general purpose registers, as pushed on the stack by the PUSHA instruction.
        struct alignas(2) [[gnu::packed]] cpu_registers
        {
            union
            {
                std::uint32_t edi;
                std::uint16_t di;
            };
            union
            {
                std::uint32_t esi;
                std::uint16_t si;
            };
            union
            {
                std::uint32_t ebp;
                std::uint16_t bp;
            };
            unsigned : 32;  // esp, not used
            union
            {
                std::uint32_t ebx;
                std::uint16_t bx;
                struct { std::uint8_t bl, bh; };
            };
            union
            {
                std::uint32_t edx;
                std::uint16_t dx;
                struct { std::uint8_t dl, dh; };
            };
            union
            {
                std::uint32_t ecx;
                std::uint16_t cx;
                struct { std::uint8_t cl, ch; };
            };
            union
            {
                std::uint32_t eax;
                std::uint16_t ax;
                struct { std::uint8_t al, ah; };
            };

            void print(FILE* out = stderr) const
            {
                fmt::print(out, "eax={:0>8x} ebx={:0>8x} ecx={:0>8x} edx={:0>8x}\n"
                                "edi={:0>8x} esi={:0>8x} ebp={:0>8x}\n",
                           eax, ebx, ecx, edx,
                           edi, esi, ebp);
            }
        };

        static_assert(sizeof(cpu_registers) == 0x20);
    }
}

#include <jw/dpmi/detail/dpmi.h>
