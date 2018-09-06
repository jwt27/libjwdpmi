/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#include <jw/dpmi/memory.h>

namespace jw
{
    namespace dpmi
    {
        bool memory_base::new_alloc_supported { true };
        bool device_memory_base::device_map_supported { true };
        bool mapped_dos_memory_base::dos_map_supported { true };

        ldt_access_rights::ldt_access_rights(selector sel)
        {
            std::uint32_t r;
            bool z;
            asm("lar %k1, %2;"
                : "=@ccz" (z)
                , "=r" (r)
                : "rm" (static_cast<std::uint32_t>(sel)));
            if (!z) throw dpmi_error(invalid_segment, __PRETTY_FUNCTION__);
            access_rights = r >> 8;
        }

        void ldt_access_rights::set(selector sel) const
        {
            dpmi_error_code error;
            bool c;
            asm volatile(
                "int 0x31;"
                : "=@ccc" (c)
                , "=a" (error)
                : "a" (0x0009)
                , "b" (sel)
                , "c" (access_rights)
                : "memory");
            if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
        }

        ldt_access_rights ldt_entry::get_access_rights() { return ldt_access_rights { sel }; }

        ldt_entry ldt_entry::clone_segment(selector s)
        {
            ldt_entry ldt { s };
            ldt.allocate();
            ldt.write();
            return ldt;
        }

        ldt_entry ldt_entry::create_segment(std::uintptr_t linear_base, std::size_t limit)
        {
            ldt_entry ldt = clone_segment(get_ds());
            ldt.allocate();
            ldt.set_base(linear_base);
            ldt.set_limit(limit);
            ldt.read();
            return ldt;
        }

        ldt_entry ldt_entry::create_alias(selector s)
        {
            selector new_sel;
            bool c;
            asm volatile(
                "int 0x31;"
                : "=@ccc" (c)
                , "=a" (new_sel)
                : "a" (0x000A)
                , "b" (s)
                : "memory");
            if (c) throw dpmi_error(new_sel, __PRETTY_FUNCTION__);
            ldt_entry ldt { new_sel };
            ldt.no_alloc = false;
            return ldt;
        }

        ldt_entry ldt_entry::create_call_gate(selector code_seg, std::uintptr_t entry_point)
        {
            split_uint32_t entry { reinterpret_cast<std::uintptr_t>(entry_point) };
            ldt_entry ldt { };
            ldt.allocate();
            auto& c = ldt.call_gate;
            c.not_system_segment = false;
            c.privilege_level = 3;
            c.type = call_gate32;
            c.is_present = true;
            c.cs = code_seg;
            c.offset_lo = entry.lo;
            c.offset_hi = entry.hi;
            c.stack_params = 0;
            ldt.write();
            return ldt;
        }

        ldt_entry::~ldt_entry()
        {
            if (no_alloc) return;
            dpmi_error_code error;
            bool c;
            asm volatile(
                "int 0x31;"
                : "=@ccc" (c)
                , "=a" (error)
                : "a" (0x0001)
                , "b" (sel)
                : "memory");
        }

        void ldt_entry::read() const
        {
            dpmi_error_code error;
            bool c;
            asm volatile(
                "push es;"
                "push ds;"
                "pop es;"
                "int 0x31;"
                "pop es;"
                : "=@ccc" (c)
                , "=a" (error)
                : "a" (0x000b)
                , "b" (sel)
                , "D" (this)
                : "memory");
            if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
        }

        void ldt_entry::write()
        {
            dpmi_error_code error;
            bool c;
            asm volatile(
                "push es;"
                "push ds;"
                "pop es;"
                "int 0x31;"
                "pop es;"
                : "=@ccc" (c)
                , "=a" (error)
                : "a" (0x000c)
                , "b" (sel)
                , "D" (this)
                : "memory");
            if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
        }

        std::uintptr_t ldt_entry::get_base(selector seg)
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

        void ldt_entry::set_base(selector seg, std::uintptr_t linear_base)
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

        std::size_t ldt_entry::get_limit(selector sel)
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

        void ldt_entry::set_limit(selector sel, std::size_t limit)
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

        void ldt_entry::allocate()
        {
            selector s;
            bool c;
            asm volatile(
                "int 0x31;"
                : "=@ccc" (c)
                , "=a" (s)
                : "a" (0x0000)
                , "c" (1)
                : "memory");
            if (c) throw dpmi_error(s, __PRETTY_FUNCTION__);
            no_alloc = false;
            sel = s;
        }

        void memory_base::old_alloc()
        {
            throw_if_irq();
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

        void memory_base::new_alloc(bool committed, std::uintptr_t desired_address)
        {
            if (committed) throw_if_irq();
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

        void memory_base::old_resize(std::size_t num_bytes)
        {
            throw_if_irq();
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

        void memory_base::new_resize(std::size_t num_bytes, bool committed)
        {
            if (committed) throw_if_irq();
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

        void device_memory_base::old_alloc(std::uintptr_t physical_address)
        {
            throw_if_irq();
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
                : "a" (0x0800)
                , "b" (phys.hi)
                , "c" (phys.lo)
                , "S" (new_size.hi)
                , "D" (new_size.lo)
                : "memory");
            if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
            addr = new_addr;
        }

        void device_memory_base::new_alloc(std::uintptr_t physical_address)
        {
            auto addr_start = round_down_to_page_size(physical_address);
            auto offset = physical_address - addr_start;
            auto pages = round_up_to_page_size(size) / get_page_size();
            auto offset_in_block = round_up_to_page_size(addr) - addr;
            offset += offset_in_block;
            addr += offset;
            size -= offset;
            dpmi_error_code error;
            bool c;
            asm volatile(
                "int 0x31;"
                : "=@ccc" (c)
                , "=a" (error)
                : "a" (0x0508)
                , "b" (offset_in_block)
                , "c" (pages)
                , "d" (addr_start)
                , "S" (handle)
                : "memory");
            if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
        }

        void mapped_dos_memory_base::new_alloc(std::uintptr_t dos_linear_address)
        {
            auto addr_start = round_down_to_page_size(dos_linear_address);
            offset = dos_linear_address - addr_start;
            auto pages = round_up_to_page_size(size) / get_page_size();
            auto offset_in_block = round_up_to_page_size(addr) - addr;
            addr += offset + offset_in_block;
            size -= offset + offset_in_block;
            dpmi_error_code error;
            bool c;
            asm volatile(
                "int 0x31;"
                : "=@ccc" (c)
                , "=a" (error)
                : "a" (0x0509)
                , "b" (offset_in_block)
                , "c" (pages)
                , "d" (addr_start)
                , "S" (handle)
                : "memory");
            if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
        }

        void mapped_dos_memory_base::alloc_selector()
        {
            selector sel;
            bool c;
            asm volatile(
                "int 0x31;"
                : "=@ccc" (c)
                , "=a" (sel)
                : "a" (0x0002)
                , "b" (dos_addr.segment)
                : "memory");
            if (c) throw dpmi_error(sel, __PRETTY_FUNCTION__);
            dos_handle = sel;
        }

        void dos_memory_base::dos_alloc(std::size_t num_bytes)
        {
            throw_if_irq();
            std::uint16_t new_handle;
            far_ptr16 new_addr { };
            bool c;
            asm volatile(
                "int 0x31;"
                : "=@ccc" (c)
                , "=a" (new_addr.segment)
                , "=d" (new_handle)
                : "a" (0x0100)
                , "b" (bytes_to_paragraphs(num_bytes))
                : "memory");
            if (c) throw dpmi_error(new_addr.segment, __PRETTY_FUNCTION__);
            dos_handle = new_handle;
            dos_addr = new_addr;
        }

        void dos_memory_base::dos_dealloc()
        {
            throw_if_irq();
            dpmi_error_code error;
            bool c;
            asm volatile(
                "int 0x31;"
                : "=@ccc" (c)
                , "=a" (error)
                : "a" (0x0101)
                , "d" (dos_handle)
                : "memory");
            if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
            dos_handle = null_dos_handle;
        }

        void dos_memory_base::dos_resize(std::size_t num_bytes)
        {
            throw_if_irq();
            dpmi_error_code error;
            bool c;
            asm volatile(
                "int 0x31;"
                : "=@ccc" (c)
                , "=a" (error)
                : "a" (0x0102)
                , "b" (bytes_to_paragraphs(num_bytes))
                , "d" (dos_handle)
                : "memory");
            if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
            size = num_bytes;
        }
}
}

