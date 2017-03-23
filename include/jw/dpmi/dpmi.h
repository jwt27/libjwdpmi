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

#include <iostream>
#include <iomanip>
#include <memory>
#include <cassert>

#include <jw/dpmi/dpmi_error.h>
#include <jw/dpmi/irq_check.h>
#include <jw/split_stdint.h>
#include <jw/typedef.h>

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
            static split_uint16_t ax, dx;
            static std::uint16_t bx;
            static std::uint8_t cl;
            static bool init;

            static std::uint8_t get_al() noexcept { get(); return ax.lo; }
            static std::uint8_t get_ah() noexcept { get(); return ax.hi; }
            static std::uint16_t get_bx() noexcept { get(); return bx; }
            static std::uint8_t get_cl() noexcept { get(); return cl; }
            static std::uint8_t get_dl() noexcept { get(); return dx.lo; }
            static std::uint8_t get_dh() noexcept { get(); return dx.hi; }

            static void get() noexcept
            {
                if (init) return;
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
            static bool init;
            static bool sup;
            static std::uint16_t raw_flags;
            static std::array<byte, 128> raw_vendor_info;

            static std::uint16_t get_flags() noexcept { get(); return raw_flags; }
            static std::array<byte, 128> get_vendor_info() noexcept { get(); return raw_vendor_info; }
            static bool get_supported() noexcept { get(); return sup; }
            static void get() noexcept
            {
                if (init || !sup) return;
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
            //constexpr far_ptr16(split_uint32_t far_ptr) noexcept : far_ptr16(far_ptr.hi, far_ptr.lo) { }
        };

        struct alignas(2) [[gnu::packed]] far_ptr32
        {
            std::uintptr_t offset;
            selector segment;

            constexpr far_ptr32(selector seg = 0, std::uintptr_t off = 0) noexcept : offset(off), segment(seg) { }
        };

        // Call a function which returns with RETF
        inline void call_far(far_ptr32 ptr)
        {
            asm volatile(
                "pusha;"
                "call fword ptr %0;"
                "popa;"
                :: "m" (ptr)
                :"esp", "memory");
        }

        // Call a function which returns with IRET
        inline void call_far_iret(far_ptr32 ptr)
        {
            asm volatile(
                "pusha;"
                "pushf;"
                "call fword ptr %0;"
                "popa;"
                :: "m" (ptr)
                :"esp", "memory");
        }

        // Yield execution to the host (used when running in a multi-tasking OS)
        // Don't confuse this with jw::thread::yield() !
        inline void yield()
        {
            if (in_irq_context()) return;
            asm volatile("int 0x2f;"::"a"(0x1680));
        }

        struct memory
        {
            //DPMI 0.9 AX=0006
            static std::uintptr_t get_selector_base_address(selector seg)  //TODO: cache cs/ss/ds //TODO: move to ldt_entry?
            {
                dpmi_error_code error;
                split_uint32_t base;
                bool c;

                asm("int 0x31;"
                    : "=@ccc" (c)
                    , "=a" (error)
                    , "=c" (base.hi)
                    , "=d" (base.lo)
                    : "a" (0x0006)
                    , "b" (seg));
                if (c) throw dpmi_error(error, "get_selector_base_address");

                return base;
            }

            //DPMI 0.9 AX=0007
            static void set_selector_base_address(selector seg, std::uintptr_t linear_base)
            {
                dpmi_error_code error;
                split_uint32_t base { linear_base };
                bool c;

                asm volatile(
                    "int 0x31;"
                    : "=@ccc" (c)
                    , "=a" (error)
                    : "a" (0x0007)
                    , "b" (seg)
                    , "c" (base.hi)
                    , "d" (base.lo));
                if (c) throw dpmi_error(error, "set_selector_base_address");
            }

            //DPMI 0.9 AX=0604
            static std::size_t get_page_size()
            {
                static std::size_t page_size = 0;
                if (page_size > 0) return page_size;

                dpmi_error_code error;
                split_uint32_t size;
                bool c;

                asm("int 0x31;"
                    : "=@ccc" (c)
                    , "=a" (error)
                    , "=b" (size.hi)
                    , "=c" (size.lo)
                    : "a" (0x0604));
                if (c) throw dpmi_error(error, "get_page_size");

                page_size = size;
                return page_size;
            }

            static std::size_t get_selector_limit(selector sel = get_ds())
            {
                std::size_t limit;
                asm("lsl %0, %1;"
                    : "=r" (limit)
                    : "rm" (sel)
                    : "cc");
                return limit;
            }

            //DPMI 0.9 AX=0008
            static void set_selector_limit(selector sel, std::size_t limit)
            {
                dpmi_error_code error;
                split_uint32_t _limit = (limit > 1_MB) ? round_up_to_page_size(limit) - 1 : limit;
                bool c;

                asm volatile(
                    "int 0x31;"
                    : "=@ccc" (c)
                    , "=a" (error)
                    : "a" (0x0008)
                    , "b" (sel)
                    , "c" (_limit.hi)
                    , "d" (_limit.lo));
                if (c) throw dpmi_error(error, "set_selector_limit");
            }

            template <typename T>
            static std::uintptr_t get_linear_address(selector seg, const T* ptr)
            {
                return get_selector_base_address(seg) + reinterpret_cast<std::uintptr_t>(ptr);
            }

            static std::size_t round_up_to_page_size(std::size_t num_bytes)
            {
                std::size_t page = get_page_size();
                return (num_bytes / page) * page + page;
            }
            static std::size_t round_down_to_page_size(std::size_t num_bytes)
            {
                std::size_t page = get_page_size();
                return (num_bytes / page) * page;
            }

            static constexpr std::uintptr_t conventional_to_linear(std::uint16_t segment, std::uint16_t offset)
            {
                return segment * 0x10 + offset;
            }

            static constexpr std::uintptr_t conventional_to_linear(far_ptr16 addr)
            {
                return conventional_to_linear(addr.segment, addr.offset);
            }

            static constexpr far_ptr16 linear_to_conventional(std::uintptr_t address)
            {
                return far_ptr16(address / 0x10, address % 0x10); //TODO: round?
            }

            static constexpr std::size_t bytes_to_paragraphs(std::size_t num_bytes)
            {
                return num_bytes / 0x10 + (num_bytes % 0x10 > 0) ? 1 : 0;
            }

            static constexpr std::size_t paragraphs_to_bytes(std::size_t num_paragraphs)
            {
                return num_paragraphs * 0x10;
            }

            static constexpr std::size_t round_up_to_paragraph_size(std::size_t num_bytes)
            {
                return (num_bytes / 0x10) * 0x10 + 0x10;
            }

            static constexpr std::size_t round_down_to_paragraph_size(std::size_t num_bytes)
            {
                return (num_bytes / 0x10) * 0x10;
            }

            static std::uintptr_t linear_to_near(std::uintptr_t address, selector sel = get_ds())
            {
                return address - memory::get_selector_base_address(sel);
            }

            template <typename T>
            static T* linear_to_near(std::uintptr_t address, selector sel = get_ds())
            {
                return static_cast<T*>(address + memory::get_selector_base_address(sel));
            }

            static std::uintptr_t near_to_linear(std::uintptr_t address, selector sel = get_ds())
            {
                return address + memory::get_selector_base_address(sel);
            }

            template <typename T>
            static std::uintptr_t near_to_linear(T* address, selector sel = get_ds())
            {
                return reinterpret_cast<std::uintptr_t>(address) + memory::get_selector_base_address(sel);
            }

        public:
            constexpr std::uintptr_t get_address() const { return addr; }
            constexpr std::size_t get_size() const { return size; }
            constexpr std::uint32_t get_handle() const { return handle; }

            template <typename T>
            T* get_ptr(selector sel = get_ds()) const
            {
                std::uintptr_t start = addr - memory::get_selector_base_address(sel);
                //std::cout << "get_ptr:" << std::hex << addr << "(near) = " << start << "(linear)" << std::endl;
                //std::cout << reinterpret_cast<T* const>(start) << std::endl;
                return reinterpret_cast<T* const>(start);
            }

            constexpr memory() : memory(0, 0) { }

            template<typename T>
            memory(selector seg, T* ptr, std::size_t num_elements = 1)
                : memory(memory::get_linear_address(seg, ptr), num_elements * sizeof(T), 0) { }

            //memory(selector seg, void(*ptr)(), std::size_t num_bytes)
            //    : memory(seg, reinterpret_cast<const void*>(ptr), num_bytes) { }

            memory(selector seg, const void* ptr, std::size_t num_bytes)
                : memory(memory::get_linear_address(seg, ptr), num_bytes, 0) { }

            constexpr memory(std::uintptr_t address, std::size_t num_bytes, std::uint32_t dpmi_handle = 0)
                : addr(address), size(num_bytes), handle(dpmi_handle) { }

            constexpr memory(const memory&) = default;
            memory& operator=(const memory&) = default;
            memory(memory&& m) : addr(m.addr), size(m.size), handle(m.handle) { m.handle = 0; }
            memory& operator=(memory&& m)
            {
                if (this != &m)
                {
                    addr = m.addr;
                    size = m.size;
                    handle = m.handle;
                    m.handle = 0;
                }
                return *this;
            }

            //DPMI 0.9 AX=0600
            void lock_memory() const
            {
                dpmi_error_code error;
                split_uint32_t _addr = addr, _size = size;
                bool c;

                asm volatile(
                    "int 0x31;"
                    : "=@ccc" (c)
                    , "=a" (error)
                    : "a" (0x0600)
                    , "b" (_addr.hi)
                    , "c" (_addr.lo)
                    , "S" (_size.hi)
                    , "D" (_size.lo));
                if (c) throw dpmi_error(error, "lock_memory");
            }

            //DPMI 0.9 AX=0601
            void unlock_memory() const
            {
                dpmi_error_code error;
                split_uint32_t _addr = addr, _size = size;
                bool c;

                asm volatile(
                    "int 0x31;"
                    : "=@ccc" (c)
                    , "=a" (error)
                    : "a" (0x0601)
                    , "b" (_addr.hi)
                    , "c" (_addr.lo)
                    , "S" (_size.hi)
                    , "D" (_size.lo));
                if (c) throw dpmi_error(error, "unlock_memory");
            }

        protected:
            std::uintptr_t addr;
            std::size_t size;
            std::uint32_t handle;
        };

        // All general purpose registers, as pushed on the stack by the PUSHA instruction.
        struct alignas(2) [[gnu::packed]] cpu_registers
        {
            union [[gnu::packed]]
            {
                std::uint32_t edi;
                std::uint16_t di;
            };
            union [[gnu::packed]]
            {   
                std::uint32_t esi;
                std::uint16_t si;
            };
            union [[gnu::packed]]
            {
                std::uint32_t ebp;
                std::uint16_t bp;
            };
            unsigned : 32;  // esp, not used
            union [[gnu::packed]]
            {
                std::uint32_t ebx;
                struct[[gnu::packed]] { std::uint16_t bx; };
                struct[[gnu::packed]] { std::uint8_t bl, bh; };
            };
            union [[gnu::packed]]
            {
                std::uint32_t edx;
                struct[[gnu::packed]] { std::uint16_t dx; };
                struct[[gnu::packed]] { std::uint8_t dl, dh; };
            };
            union [[gnu::packed]]
            {
                std::uint32_t ecx;
                struct[[gnu::packed]] { std::uint16_t cx; };
                struct[[gnu::packed]] { std::uint8_t cl, ch; };
            }; 
            union [[gnu::packed]]
            {
                std::uint32_t eax;
                struct[[gnu::packed]] { std::uint16_t ax; };
                struct[[gnu::packed]] { std::uint8_t al, ah; };
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
        };

        // CPU register structure for DPMI real-mode functions.
        struct alignas(2) [[gnu::packed]] rm_registers : public cpu_registers
        {
            union [[gnu::packed]]
            {
                unsigned flags_reg : 16;
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
                } flags;
            };
            unsigned es : 16, ds : 16, fs : 16, gs : 16;
            unsigned ip : 16, cs : 16; // not used in call_rm_interrupt()
            unsigned sp : 16, ss : 16; // not required for call_rm_interrupt()

            auto& print(std::ostream& out) const
            {
                using namespace std;
                out << hex << setfill('0');
                out << "es=" << setw(4) << es << " ds=" << setw(4) << ds << " fs=" << setw(4) << fs << " gs=" << setw(4) << gs << "\n";
                out << "cs=" << setw(4) << cs << " ip=" << setw(4) << ip << " ss=" << setw(4) << ss << " sp=" << setw(4) << sp << " flags=" << setw(4) << flags_reg << "\n";
                out << hex << setfill(' ') << setw(0) << flush;
                return out;
            }
            friend auto& operator<<(std::ostream& out, const rm_registers& in) { return in.print(out); }
        };

        inline void call_rm_interrupt(std::uint8_t interrupt, rm_registers* reg)
        {
            selector new_reg_ds = get_ds();
            rm_registers* new_reg;
            dpmi_error_code error;
            bool c;

            asm volatile(
                "mov es, %w2;"
                "int 0x31;"
                "mov %w2, es;"
                : "=@ccc" (c)
                , "=a" (error)
                , "+rm" (new_reg_ds)
                , "=D" (new_reg)
                : "a" (0x0300)
                , "b" (interrupt)
                , "D" (reg)
                , "c" (0)   // TODO: stack?
                : "memory");
            if (c) throw dpmi_error(error, "call_rm_interrupt");

            if (new_reg != reg || new_reg_ds != get_ds())   // copy back if location changed.
                *reg = *(memory(new_reg_ds, new_reg).get_ptr<rm_registers>());
        }

        static_assert(sizeof(cpu_registers) == 0x20, "check sizeof struct dpmi::cpu_registers");
        static_assert(sizeof( rm_registers) == 0x32, "check sizeof struct dpmi::rm_registers");
    }
}
