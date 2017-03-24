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
#include <jw/dpmi/dpmi.h>

namespace jw
{
    namespace dpmi
    {
        //DPMI 0.9 AX=0006
        [[gnu::pure]] inline std::uintptr_t get_selector_base_address(selector seg)  //TODO: cache cs/ss/ds
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
        inline void set_selector_base_address(selector seg, std::uintptr_t linear_base)
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
        [[gnu::pure]] inline std::size_t get_page_size()
        {
            static std::size_t page_size { 0 };
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

        [[gnu::pure]] inline std::size_t round_down_to_page_size(std::size_t num_bytes)
        {
            std::size_t page = get_page_size();
            return (num_bytes / page) * page;
        }

        [[gnu::pure]] inline std::size_t round_up_to_page_size(std::size_t num_bytes)
        {
            std::size_t page = get_page_size();
            return round_down_to_page_size(num_bytes) + page;
        }

        inline std::size_t get_selector_limit(selector sel = get_ds())
        {
            std::size_t limit;
            bool z;
            asm("lsl %1, %2;"
                : "=@ccz" (z)
                , "=r" (limit)
                : "rm" (static_cast<std::uint32_t>(sel))
                : "cc");
            if (z) throw dpmi_error(invalid_segment, "get_selector_limit");
            return limit;
        }

        //DPMI 0.9 AX=0008
        inline void set_selector_limit(selector sel, std::size_t limit)
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
        [[gnu::pure]] inline std::uintptr_t get_linear_address(selector seg, const T* ptr)
        {
            return get_selector_base_address(seg) + reinterpret_cast<std::uintptr_t>(ptr);
        }
            
        inline constexpr std::uintptr_t conventional_to_linear(std::uint16_t segment, std::uint16_t offset) noexcept
        {
            return (segment << 4) + offset;
        }

        inline constexpr std::uintptr_t conventional_to_linear(far_ptr16 addr) noexcept
        {
            return conventional_to_linear(addr.segment, addr.offset);
        }

        inline constexpr far_ptr16 linear_to_conventional(std::uintptr_t address) noexcept
        {
            return far_ptr16(address >> 4, address & 0x0f); //TODO: round?
        }

        inline constexpr std::size_t bytes_to_paragraphs(std::size_t num_bytes) noexcept
        {
            return (num_bytes >> 4) + ((num_bytes & 0x0f) > 0) ? 1 : 0;
        }

        inline constexpr std::size_t paragraphs_to_bytes(std::size_t num_paragraphs) noexcept
        {
            return num_paragraphs << 4;
        }

        inline constexpr std::size_t round_down_to_paragraph_size(std::size_t num_bytes) noexcept
        {
            return num_bytes & ~0x10;
        }
            
        inline constexpr std::size_t round_up_to_paragraph_size(std::size_t num_bytes) noexcept
        {
            return round_down_to_paragraph_size(num_bytes) + 0x10;
        }

        [[gnu::pure]] inline std::uintptr_t linear_to_near(std::uintptr_t address, selector sel = get_ds())
        {
            return address - get_selector_base_address(sel);
        }

        template <typename T>
        [[gnu::pure]] inline T* linear_to_near(std::uintptr_t address, selector sel = get_ds())
        {
            return static_cast<T*>(address + get_selector_base_address(sel));
        }

        [[gnu::pure]] inline std::uintptr_t near_to_linear(std::uintptr_t address, selector sel = get_ds())
        {
            return address + get_selector_base_address(sel);
        }

        template <typename T>
        [[gnu::pure]] inline std::uintptr_t near_to_linear(T* address, selector sel = get_ds())
        {
            return reinterpret_cast<std::uintptr_t>(address) + get_selector_base_address(sel);
        }

        struct memory
        {
            constexpr std::uintptr_t get_linear_address() const { return addr; }
            constexpr std::size_t get_size() const { return size; }

            template <typename T>
            [[gnu::pure]] T* get_ptr(selector sel = get_ds()) const
            {
                std::uintptr_t start = addr - get_selector_base_address(sel);
                return reinterpret_cast<T*>(start);
            }

            constexpr memory() : memory(0, 0) { }

            template<typename T, std::enable_if_t<!std::is_void<T>::value, bool> = { }>
            memory(selector seg, const T* ptr, std::size_t num_elements = 1)
                : memory(dpmi::get_linear_address(seg, ptr), num_elements * sizeof(T)) { }

            memory(selector seg, const void* ptr, std::size_t num_bytes)
                : memory(dpmi::get_linear_address(seg, ptr), num_bytes) { }

            constexpr memory(std::uintptr_t address, std::size_t num_bytes) noexcept
                : addr(address), size(num_bytes) { }

            constexpr memory(const memory&) noexcept = default;
            constexpr memory& operator=(const memory&) noexcept = default;
            constexpr memory(memory&& m) noexcept = default;
            constexpr memory& operator=(memory&& m) noexcept = default;

            //DPMI 0.9 AX=0600
            void lock_memory()
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
            void unlock_memory()
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
        };

        struct raw_memory_base : public memory
        {
            constexpr std::uint32_t get_handle() const { return handle; }

        protected:
            std::uint32_t handle;
        };

        template <typename T>
        struct raw_memory : public raw_memory_base
        {

        };
    }
}
