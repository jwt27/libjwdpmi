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
#include <limits>
#include <jw/dpmi/dpmi.h>

namespace jw
{
    namespace dpmi
    {
        //DPMI 0.9 AX=0006
        [[gnu::pure]] inline std::uintptr_t get_selector_base(selector seg = get_ds())
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
            if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);

            return base;
        }

        //DPMI 0.9 AX=0007
        inline void set_selector_base(selector seg, std::uintptr_t linear_base)
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
                , "d" (base.lo)
                : "memory");
            if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
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
            if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);

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
            if (!z) throw dpmi_error(invalid_segment, __PRETTY_FUNCTION__);
            return limit;
        }

        //DPMI 0.9 AX=0008
        inline void set_selector_limit(selector sel, std::size_t limit)
        {
            dpmi_error_code error;
            split_uint32_t _limit = (limit >= 1_MB) ? round_up_to_page_size(limit) - 1 : limit;
            bool c;

            asm volatile(
                "int 0x31;"
                : "=@ccc" (c)
                , "=a" (error)
                : "a" (0x0008)
                , "b" (sel)
                , "c" (_limit.hi)
                , "d" (_limit.lo)
                : "memory");
            if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
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
            return address - get_selector_base(sel);
        }

        template <typename T>
        [[gnu::pure]] inline T* linear_to_near(std::uintptr_t address, selector sel = get_ds())
        {
            return reinterpret_cast<T*>(linear_to_near(address, sel));
        }

        [[gnu::pure]] inline std::uintptr_t near_to_linear(std::uintptr_t address, selector sel = get_ds())
        {
            return address + get_selector_base(sel);
        }

        template <typename T>
        [[gnu::pure]] inline std::uintptr_t near_to_linear(T* address, selector sel = get_ds())
        {
            return near_to_linear(reinterpret_cast<std::uintptr_t>(address), sel);
        }

        struct linear_memory
        {
            constexpr std::uintptr_t get_address() const noexcept { return addr; }
            virtual std::size_t get_size() const noexcept { return size; }

            template <typename T>
            [[gnu::pure]] T* get_ptr(selector sel = get_ds())
            {
                return linear_to_near<T>(addr, sel);
            }

            template <typename T>
            [[gnu::pure]] const T* get_ptr(selector sel = get_ds()) const
            {
                return get_ptr<const T>(sel);
            }

            constexpr linear_memory() : linear_memory(0, 0) { }

            template<typename T, std::enable_if_t<!std::is_void<T>::value, bool> = { }>
            linear_memory(selector seg, const T* ptr, std::size_t num_elements = 1)
                : linear_memory(dpmi::near_to_linear(ptr, seg), num_elements * sizeof(T)) { }

            linear_memory(selector seg, const void* ptr, std::size_t num_bytes)
                : linear_memory(dpmi::near_to_linear(ptr, seg), num_bytes) { }

            constexpr linear_memory(std::uintptr_t address, std::size_t num_bytes) noexcept
                : addr(address), size(num_bytes) { }

            constexpr linear_memory(const linear_memory&) noexcept = default;
            constexpr linear_memory& operator=(const linear_memory&) noexcept = default;
            constexpr linear_memory(linear_memory&& m) noexcept = default;
            constexpr linear_memory& operator=(linear_memory&& m) noexcept = default;

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
                if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
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
                if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
            }

        protected:
            std::uintptr_t addr;
            std::size_t size;
        };

        // *** anything below this line is work-in-progress *** //

        struct no_alloc_tag { };

        struct memory_base : public linear_memory
        {
            memory_base(const linear_memory& mem, bool committed = true) : linear_memory(mem)
            {
                allocate(committed, addr);
            }

            memory_base(std::size_t num_bytes, bool committed = true) : memory_base(linear_memory { 0, num_bytes }, committed) { }

            virtual ~memory_base() { deallocate(); }

            memory_base(const memory_base&) = delete;
            memory_base& operator=(const memory_base&) = delete;

            memory_base(memory_base&& m) noexcept : linear_memory(m), handle(m.handle) { m.handle = null_handle; }
            memory_base& operator=(memory_base&& m) noexcept
            {
                std::swap(handle, m.handle);
                std::swap(size, m.size);
                std::swap(addr, m.addr);
                return *this;
            }

            memory_base& operator=(linear_memory&&) = delete;
            memory_base& operator=(const linear_memory&) = delete;

            virtual void resize(std::size_t num_bytes, bool committed = true)
            {
                if (new_alloc_supported) new_resize(num_bytes, committed);
                else old_resize(num_bytes);
            }

            std::uint32_t get_handle() const noexcept { return handle; }
            virtual operator bool() const noexcept { return handle != null_handle; }

        protected:
            constexpr memory_base(no_alloc_tag, const linear_memory& mem) noexcept : linear_memory(mem) { }
            constexpr memory_base(no_alloc_tag, std::size_t num_bytes) noexcept : memory_base(no_alloc_tag { }, linear_memory { 0, num_bytes }) { }

            virtual void allocate(bool committed = true, std::uintptr_t desired_address = 0)
            {
                if (new_alloc_supported) try
                {
                    new_alloc(committed, desired_address);
                    return;
                }
                catch (const dpmi_error& e)
                {
                    switch (e.code().value())
                    {
                    default: throw;
                    case unsupported_function:
                    case 0x0504:
                        new_alloc_supported = false;
                    }
                }
                old_alloc();
            }

            virtual void deallocate()
            {
                if (handle == null_handle) return;
                split_uint32_t _handle { handle };
                dpmi_error_code error;
                bool c;
                asm volatile(
                    "int 0x31;"
                    : "=@ccc" (c)
                    , "=a" (error)
                    : "a" (0x0502)
                    , "S" (_handle.hi)
                    , "D" (_handle.lo)
                    : "memory");
                if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
                handle = null_handle;
            }

            bool is_valid_address(std::uintptr_t address)
            {
                if (address <= get_selector_base()) return false;
                //if (get_selector_limit() < linear_to_near(address + size)) set_selector_limit(get_ds(), address + size);
                if (get_selector_limit() < linear_to_near(address + size)) set_selector_limit(get_ds(), get_selector_limit() * 2);
                return true;
            }

            static bool new_alloc_supported;
            static constexpr std::uint32_t null_handle { std::numeric_limits<std::uint32_t>::max() };
            std::uint32_t handle { null_handle };

        private:
            void old_alloc()
            {
                if (handle != null_handle) deallocate();
                split_uint32_t new_size { size };
                split_uint32_t new_addr, new_handle;
                dpmi_error_code error;
                bool c;
                do
                {
                    asm volatile(
                        "int 0x31;"
                        : "=@ccc" (c)
                        , "=a" (error)
                        , "=b" (new_addr.hi)
                        , "=c" (new_addr.lo)
                        , "=S" (new_handle.hi)
                        , "=D" (new_handle.lo)
                        : "a" (0x0501)
                        , "b" (new_size.hi)
                        , "c" (new_size.lo)
                        : "memory");
                    if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
                } while (!is_valid_address(new_addr));
                handle = new_handle;
                addr = new_addr;
            }

            void new_alloc(bool committed, std::uintptr_t desired_address)
            {
                if (handle != null_handle) deallocate();
                std::uint32_t new_handle;
                std::uintptr_t new_addr;
                dpmi_error_code error;
                bool c;
                do
                {
                    asm volatile(
                        "int 0x31;"
                        : "=@ccc" (c)
                        , "=a" (error)
                        , "=b" (new_addr)
                        , "=S" (new_handle)
                        : "a" (0x0504)
                        , "b" (desired_address)
                        , "c" (size)
                        , "d" (static_cast<std::uint32_t>(committed))
                        : "memory");
                    if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
                } while (!is_valid_address(new_addr));
                handle = new_handle;
                addr = new_addr;
            }

            void old_resize(std::size_t num_bytes)
            {
                if (handle != null_handle) deallocate();
                split_uint32_t new_size { num_bytes };
                split_uint32_t new_handle { handle };
                split_uint32_t new_addr;
                dpmi_error_code error;
                bool c;
                do
                {
                    asm volatile(
                        "int 0x31;"
                        : "=@ccc" (c)
                        , "=a" (error)
                        , "=b" (new_addr.hi)
                        , "=c" (new_addr.lo)
                        , "+S" (new_handle.hi)
                        , "+D" (new_handle.lo)
                        : "a" (0x0503)
                        , "b" (new_size.hi)
                        , "c" (new_size.lo)
                        : "memory");
                    if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
                } while (!is_valid_address(new_addr));
                handle = new_handle;
                addr = new_addr;
                size = new_size;
            }

            void new_resize(std::size_t num_bytes, bool committed)
            {
                std::uint32_t new_handle { handle };
                std::uintptr_t new_addr;
                dpmi_error_code error;
                bool c;
                do
                {
                    asm volatile(
                        "int 0x31;"
                        : "=@ccc" (c)
                        , "=a" (error)
                        , "=b" (new_addr)
                        , "+S" (new_handle)
                        : "a" (0x0505)
                        , "c" (num_bytes)
                        , "d" (static_cast<std::uint32_t>(committed))
                        : "memory");
                    if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
                } while (!is_valid_address(new_addr));
                handle = new_handle;
                addr = new_addr;
            }
        };

        struct device_memory_base : public memory_base
        {
            using base = memory_base;

            device_memory_base(std::size_t num_bytes, std::uintptr_t physical_address)
                : base(no_alloc_tag { }, round_up_to_page_size(num_bytes))
            {
                allocate(physical_address);
            }

            device_memory_base(const base&) = delete;

            virtual void resize(std::size_t, bool = true) override { }

            bool requires_new_selector() { return !device_map_supported; }

        protected:
            virtual void allocate(std::uintptr_t physical_address)
            {
                if (!new_alloc_supported) device_map_supported = false;
                if (device_map_supported)
                {
                    capabilities c { };
                    if (!c.supported || !c.flags.device_mapping) device_map_supported = false;
                }
                if (device_map_supported)
                {
                    base::allocate(false);
                    new_alloc(physical_address);
                }
                else old_alloc(physical_address);
            }

            virtual void deallocate() override
            {
                if (device_map_supported)
                {
                    base::deallocate();
                    return;
                }
                else
                {
                    split_uint32_t old_addr { addr };
                    asm volatile(
                        "int 0x31;"
                        :: "a" (0x0801)
                        , "b" (old_addr.hi)
                        , "c" (old_addr.lo)
                        : "memory");
                    // This is an optional dpmi 1.0 function. don't care if this fails.
                }
            }

            static bool device_map_supported;

        private:
            void old_alloc(std::uintptr_t physical_address)
            {
                split_uint32_t new_addr;
                split_uint32_t new_size { size };
                split_uint32_t phys { physical_address };
                dpmi_error_code error;
                bool c;
                asm volatile(
                    "int 0x31;"
                    : "=@ccc" (c)
                    , "=a" (error)
                    , "=b" (new_addr.hi)
                    , "=c" (new_addr.lo)
                    : "b" (phys.hi)
                    , "c" (phys.lo)
                    , "S" (new_size.hi)
                    , "D" (new_size.lo)
                    : "memory");
                if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
                addr = new_addr;
            }

            void new_alloc(std::uintptr_t physical_address)
            {
                dpmi_error_code error;
                bool c;
                asm volatile(
                    "int 0x31;"
                    : "=@ccc" (c)
                    , "=a" (error)
                    : "a" (0x0508)
                    , "b" (round_up_to_page_size(addr) - addr)
                    , "c" (round_up_to_page_size(size) / get_page_size())
                    , "d" (physical_address)
                    , "S" (handle)
                    : "memory");
                if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
            }
        };

        template <typename T, typename base = memory_base>
        struct memory : public base
        {
            template<typename... Args>
            memory(std::size_t num_elements, Args&&... args) : base(num_elements * sizeof(T), std::forward<Args>(args)...) { }
            
            [[gnu::pure]] auto* get_ptr(selector sel = get_ds()) { return base::template get_ptr<T>(sel); }
            [[gnu::pure]] auto* operator->() noexcept { return get_ptr(); }
            auto& operator*() noexcept { return *get_ptr(); }
            auto& operator[](std::ptrdiff_t i) noexcept { return *(get_ptr() + i); }

            [[gnu::pure]] const auto* get_ptr(selector sel = get_ds()) const { return base::template get_ptr<T>(sel); }
            [[gnu::pure]] const auto* operator->() const noexcept { return get_ptr(); }
            const auto& operator*() const noexcept { return *get_ptr(); }
            const auto& operator[](std::ptrdiff_t i) const noexcept { return *(get_ptr() + i); }

            template<typename... Args>
            void resize(std::size_t num_elements, Args&&... args) { base::resize(num_elements * sizeof(T), std::forward<Args>(args)...); }
            virtual std::size_t get_size() const noexcept override { return base::get_size() / sizeof(T); }
        };

        template <typename T = byte> using raw_memory = memory<T, memory_base>;
        template <typename T = byte> using device_memory = memory<T, device_memory_base>;
    }
}
