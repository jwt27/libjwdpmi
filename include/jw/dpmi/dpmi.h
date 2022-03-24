/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once

#include <iostream>
#include <iomanip>
#include <memory>
#include <cassert>
#include <fmt/core.h>
#include <jw/address_space.h>
#include <jw/dpmi/dpmi_error.h>
#include <jw/dpmi/irq_check.h>
#include <jw/split_int.h>
#include <jw/common.h>

namespace jw
{
    namespace dpmi
    {
        using selector = std::uint16_t;

    #define GET_SEG_REG(reg)                \
        selector s;                         \
        asm ("mov %w0, "#reg";":"=rm" (s)); \
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
            union flags_t
            {
                struct
                {
                    bool host_is_32bit : 1;
                    bool reflect_int_to_real_mode : 1;
                    bool supports_virtual_memory : 1;
                    unsigned : 13;
                };
                constexpr flags_t(auto v) noexcept :raw(v) { }
            private:
                std::uint16_t raw;
            } const flags;

            union cpu_type_t
            {
                enum
                {
                    cpu_i286 = 2,
                    cpu_i386 = 3,
                    cpu_i486 = 4,
                    cpu_i586 = 5,
                    cpu_i686 = 6
                };
                constexpr cpu_type_t(auto v) noexcept :raw(v) { }
            private:
                std::uint8_t raw;
            } const cpu_type;

            const std::uint8_t major, minor;
            const std::uint8_t pic_master_base, pic_slave_base;

            version() noexcept : flags(get_bx()), cpu_type(get_cl())
                , major(get_ah()), minor(get_al())
                , pic_master_base(get_dh()), pic_slave_base(get_dl()) { }

        private:
            static inline split_uint16_t ax, dx;
            static inline std::uint16_t bx;
            static inline std::uint8_t cl;
            static inline bool init { false };

            static std::uint8_t get_al() noexcept { get(); return ax.lo; }
            static std::uint8_t get_ah() noexcept { get(); return ax.hi; }
            static std::uint16_t get_bx() noexcept { get(); return bx; }
            static std::uint8_t get_cl() noexcept { get(); return cl; }
            static std::uint8_t get_dl() noexcept { get(); return dx.lo; }
            static std::uint8_t get_dh() noexcept { get(); return dx.hi; }

            static void get() noexcept
            {
                if (init) [[likely]] return;
                asm("int 0x31;"
                    : "=a" (ax)
                    , "=b" (bx)
                    , "=c" (cl)
                    , "=d" (dx)
                    : "a" (0x0400)
                    : "cc");
                init = true;
            }
        };

        // Get optional DPMI 1.0 capabilities of current DPMI host
        struct capabilities
        {
            const bool supported;
            union flags_t
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
                    unsigned : 9;
                };
                constexpr flags_t(auto v) noexcept :raw(v) { }
            private:
                std::uint16_t raw;
            } const flags;

            union vendor_info_t
            {
                struct [[gnu::packed]]
                {
                    struct [[gnu::packed]]
                    {
                        unsigned major : 8;
                        unsigned minor : 8;
                    } const version;
                    char name[126];
                };
                constexpr vendor_info_t(auto v) noexcept :raw(v) { }
            private:
                std::array<byte, 128> raw;
            } const vendor_info;

            capabilities() noexcept
                : supported(get_supported())
                , flags(get_flags())
                , vendor_info(get_vendor_info()) { }

        private:
            static inline bool init { false };
            static inline bool sup { true };
            static inline std::uint16_t raw_flags;
            static inline std::array<byte, 128> raw_vendor_info { };

            static std::uint16_t get_flags() noexcept { get(); return raw_flags; }
            static std::array<byte, 128> get_vendor_info() noexcept { get(); return raw_vendor_info; }
            static bool get_supported() noexcept { get(); return sup; }
            static void get() noexcept
            {
                if (init or not sup) [[likely]] return;
                bool c;
                asm("push es;"
                    "mov es, %w2;"
                    "int 0x31;"
                    "pop es;"
                    : "=a" (raw_flags)
                    , "=@ccc"(c)
                    : "r" (get_ds())
                    , "a" (0x0401)
                    , "D" (raw_vendor_info.data())
                    : "cc", "cx", "dx", "memory");
                init = true;
                if (!c) return;
                sup = false;
                raw_flags = 0;
                raw_vendor_info.fill(0);
            }
        };

        struct alignas(2) [[gnu::packed]] far_ptr16
        {
            std::uint16_t offset, segment;

            constexpr far_ptr16(selector seg = 0, std::uint16_t off = 0) noexcept : offset(off), segment(seg) { }
            friend auto& operator<<(std::ostream& out, const far_ptr16& in)
            {
                using namespace std;
                return out << hex << setfill('0') << setw(4) << in.segment << ':' << setw(4) << in.offset << setfill(' ');
            }
        };

        struct alignas(2) [[gnu::packed]] far_ptr32
        {
            std::uintptr_t offset;
            selector segment;

            constexpr far_ptr32(selector seg = 0, std::uintptr_t off = 0) noexcept : offset(off), segment(seg) { }
            friend auto& operator<<(std::ostream& out, const far_ptr32& in)
            {
                using namespace std;
                return out << hex << setfill('0') << setw(4) << in.segment << ':' << setw(8) << in.offset << setfill(' ');
            }
        };

        struct gs_override
        {
            gs_override(selector new_gs) { set_gs(new_gs); }
            ~gs_override() { set_gs(old_gs); }

            gs_override() = delete;
            gs_override(const gs_override&) = delete;
            gs_override(gs_override&&) = delete;
            gs_override& operator=(const gs_override&) = delete;
            gs_override& operator=(gs_override&&) = delete;

        private:
            void set_gs(auto s) { asm volatile("mov gs, %w0;" :: "rm" (s)); }
            selector old_gs { get_gs() };
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

            void get() { asm ("pushfd; pop %0" : "=rm" (*this)); }
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

            auto& print(std::ostream& out) const
            {
                using namespace std;
                out << hex << setfill('0');
                out << "eax=" << setw(8) << eax << " ebx=" << setw(8) << ebx << " ecx=" << setw(8) << ecx << " edx=" << setw(8) << edx << "\n";
                out << "edi=" << setw(8) << edi << " esi=" << setw(8) << esi << " ebp=" << setw(8) << ebp << "\n";
                out << hex << setfill(' ') << setw(0) << flush;
                return out;
            }
            friend auto& operator<<(std::ostream& out, const cpu_registers& in) { return in.print(out); }

            void print(FILE* out = stderr)          const { print(out, *this); }
            void print(FILE* out = stderr) __seg_fs const { print(out, *this); }

            template<any_address_space<cpu_registers> R>
            static void print(FILE* out, const R& r)
            {
                fmt::print(out, "eax={:0>8x} ebx={:0>8x} ecx={:0>8x} edx={:0>8x}\n"
                                "edi={:0>8x} esi={:0>8x} ebp={:0>8x}\n",
                           r.eax, r.ebx, r.ecx, r.edx,
                           r.edi, r.esi, r.ebp);
            }
        };

        static_assert(sizeof(cpu_registers) == 0x20);
    }
}
