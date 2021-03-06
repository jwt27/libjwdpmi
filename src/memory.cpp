/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#include <optional>
#include <jw/dpmi/memory.h>
#include <jw/dpmi/cpu_exception.h>
#include <jw/dpmi/ring0.h>

namespace jw
{
    namespace dpmi
    {
        bool memory_base::new_alloc_supported { true };
        bool device_memory_base::device_map_supported { true };
        bool mapped_dos_memory_base::dos_map_supported { true };

        std::optional<descriptor> gdt, ldt;

        [[gnu::noipa]] descriptor_data read_descriptor_direct(selector_bits s, bool use_ring0)
        {
            union
            {
                split_uint64_t raw;
                descriptor_data data;
            };
            std::optional<ring0_privilege> r0;
            if (use_ring0) r0.emplace();
            auto& table = s.local ? ldt : gdt;
            gs_override gs { table->get_selector() };
            asm ("mov %0, gs:[%1*8+0]" : "=r" (raw.lo) : "r" (s.index));
            asm ("mov %0, gs:[%1*8+4]" : "=r" (raw.hi) : "r" (s.index));
            return data;
        }

        [[gnu::noipa]] void write_descriptor_direct(selector_bits s, const descriptor_data& d, bool use_ring0)
        {
            union
            {
                split_uint64_t raw;
                descriptor_data data;
            };
            data = d;
            std::optional<ring0_privilege> r0;
            if (use_ring0) r0.emplace();
            auto& table = s.local ? ldt : gdt;
            gs_override gs { table->get_selector() };
            asm ("mov gs:[%1*8+0], %0" :: "r" (raw.lo), "r" (s.index) : "memory");
            asm ("mov gs:[%1*8+4], %0" :: "r" (raw.hi), "r" (s.index) : "memory");
        }

        struct [[gnu::packed]] gdt_register
        {
            std::uint16_t limit;
            std::uint32_t base;
        };

        [[gnu::noipa]] auto sgdt()
        {
            gdt_register gdtr;
            asm ("sgdt %0"  : "=m"  (gdtr));
            return gdtr;
        }

        [[gnu::noipa]] auto sldt()
        {
            selector ldtr;
            asm ("sldt %w0" : "=rm" (ldtr));
            return ldtr;
        }

        descriptor::direct_ldt_access_t descriptor::direct_ldt_access() noexcept
        {
            static direct_ldt_access_t have_access { unknown };
            if (have_access == unknown) [[unlikely]]
            {
                bool use_ring0 { false };
                retry:
                try
                {
                    have_access = no;

                    gdt_register gdtr;
                    selector ldtr;

                    {
                        std::optional<ring0_privilege> r0;
                        if (use_ring0) r0.emplace();
                        gdtr = sgdt();
                        ldtr = sldt();
                    }

                    gdt = descriptor::create_segment(gdtr.base, gdtr.limit + 1);
                    selector_bits ldt_selector = ldtr;

                    auto ldt_desc = read_descriptor_direct(ldt_selector, use_ring0);

                    split_uint32_t base;
                    base.lo = ldt_desc.segment.base_lo;
                    base.hi.lo = ldt_desc.segment.base_hi_lo;
                    base.hi.hi = ldt_desc.segment.base_hi_hi;
                    split_uint32_t limit { };
                    limit.lo = ldt_desc.segment.limit_lo;
                    limit.hi = ldt_desc.segment.limit_hi;
                    ldt = descriptor::create_segment(base, limit);

                    if (use_ring0) have_access = ring0;
                    else have_access = yes;

                    // Check if ldt access is possible
                    descriptor test { get_ds() };
                }
                catch (...)
                {
                    have_access = no;
                    if (not use_ring0 and ring0_privilege::wont_throw())
                    {
                        use_ring0 = true;
                        goto retry;
                    }
                    gdt.reset();
                    ldt.reset();
                }
            }
            return have_access;
        }

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

        ldt_access_rights descriptor::get_access_rights() { return ldt_access_rights { sel.value }; }

        descriptor::descriptor(descriptor&& d) noexcept
            : descriptor_data(static_cast<descriptor_data>(d)), sel(d.sel), no_alloc(d.no_alloc)
        {
            d.no_alloc = true;
        }

        descriptor& descriptor::operator=(descriptor&& d)
        {
            deallocate();
            new(this) descriptor(std::move(d));
            return *this;
        }

        descriptor& descriptor::operator=(const descriptor_data& d)
        {
            *static_cast<descriptor_data*>(this) = d;
            write();
            return *this;
        }

        descriptor descriptor::create_segment(std::uintptr_t linear_base, std::size_t limit)
        {
            descriptor ldt = clone_segment(get_ds());
            ldt.set_base(linear_base);
            ldt.set_limit(limit);
            ldt.read();
            return ldt;
        }

        descriptor descriptor::create_code_segment(std::uintptr_t linear_base, std::size_t limit)
        {
            descriptor ldt = clone_segment(get_cs());
            ldt.set_base(linear_base);
            ldt.set_limit(limit);
            ldt.read();
            return ldt;
        }

        descriptor descriptor::clone_segment(selector s)
        {
            descriptor d { s };
            d.allocate();
            d.write();
            return d;
        }

        descriptor descriptor::create_call_gate(selector code_seg, std::uintptr_t entry_point)
        {
            split_uint32_t entry { entry_point };
            descriptor d { };
            d.allocate();
            auto& c = d.call_gate;
            c.not_system_segment = false;
            c.privilege_level = 3;
            c.type = call_gate32;
            c.is_present = true;
            c.cs = code_seg;
            c.offset_lo = entry.lo;
            c.offset_hi = entry.hi;
            c.stack_params = 0;
            d.write();
            return d;
        }

        descriptor::~descriptor()
        {
            try { deallocate(); }
            catch(...) { }
        }

        void descriptor::read() const
        {
            auto ldt_access = direct_ldt_access();
            if (ldt_access != no) [[likely]]
            {
                static_cast<descriptor_data&>(*const_cast<descriptor*>(this)) = read_descriptor_direct(sel, ldt_access == ring0);
            }
            else
            {
                dpmi_error_code error;
                bool c;
                asm volatile(
                    "lea edi, %2;"
                    "push es;"
                    "push ds;"
                    "pop es;"
                    "int 0x31;"
                    "pop es;"
                    : "=@ccc" (c)
                    , "=a" (error)
                    , "=m" (*const_cast<descriptor*>(this))
                    : "a" (0x000b)
                    , "b" (sel)
                    : "edi", "memory");
                if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
            }
        }

        void descriptor::write()
        {
            auto ldt_access = direct_ldt_access();
            if (ldt_access != no) [[likely]]
            {
                write_descriptor_direct(sel, *this, ldt_access == ring0);
            }
            else
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
                    , "D" (static_cast<descriptor_data*>(this)));
                if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
            }
        }

        std::uintptr_t descriptor::get_base(selector seg)
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

        void descriptor::set_base(selector seg, std::uintptr_t linear_base)
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

        std::size_t descriptor::get_limit() const
        {
            split_uint32_t v { };
            v.hi = segment.limit_hi;
            v.lo = segment.limit_lo;
            if (segment.is_page_granular) return v << 12 | ((1 << 12) - 1);
            return v;
        }

        std::size_t descriptor::get_limit(selector sel)
        {
            if (selector_bits { sel }.privilege_level < selector_bits { get_cs() }.privilege_level)
                return descriptor { sel }.get_limit();

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

        void descriptor::set_limit(selector sel, std::size_t limit)
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

        void descriptor::allocate()
        {
            if (not no_alloc) deallocate();
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

        void descriptor::deallocate()
        {
            if (no_alloc) return;
            dpmi_error_code error;
            bool c;
            asm volatile(
                "int 0x31;"
                : "=@ccc" (c)
                , "=a" (error)
                : "a" (0x0001)
                , "b" (sel));
            if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
            no_alloc = true;
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
            auto pages = round_up_to_page_size(size) / page_size;
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

        void mapped_dos_memory_base::new_alloc(std::uintptr_t dos_physical_address)
        {
            auto addr_start = round_down_to_page_size(dos_physical_address);
            offset = dos_physical_address - addr_start;
            auto pages = round_up_to_page_size(size) / page_size;
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
