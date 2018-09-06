/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#include <jw/dpmi/memory.h>
#include <jw/dpmi/cpu_exception.h>

namespace jw
{
    namespace dpmi
    {
        bool memory_base::new_alloc_supported { true };
        bool device_memory_base::device_map_supported { true };
        bool mapped_dos_memory_base::dos_map_supported { true };

        linear_memory gdt, ldt;

        bool direct_ldt_access()
        {
            enum { unknown, yes, no } static have_access { unknown };

            if (__builtin_expect(have_access == unknown, false))
            {
                try
                {
                    have_access = no;

                    struct [[gnu::packed]]
                    {
                        std::uint16_t limit;
                        std::uint32_t base;
                    } gdtr;
                    selector ldtr;

                    asm("sgdt %0":"=m"(gdtr));
                    asm("sldt %w0":"=rm"(ldtr));

                    std::clog << "gdtr base=" << std::hex << gdtr.base << " limit=" << gdtr.limit << '\n';
                    std::clog << "ldtr selector=" << ldtr << '\n';

                    gdt = linear_memory { gdtr.base, gdtr.limit };
                    std::clog << "gdt descriptor=" << *reinterpret_cast<std::uint64_t*>(gdt.get_descriptor().lock().get()) << '\n';
                    descriptor_data ldt_desc;
                    selector_bits ldt_selector = ldtr;

                    asm("push gs;"
                        "mov gs, %w1;"
                        "lea eax, [%2*8];"
                        "mov edx, gs:[eax];"
                        "mov %0, edx;"
                        "mov edx, gs:[eax+4];"
                        "mov %0+4, edx;"
                        "pop gs;"
                        : "+m" (*reinterpret_cast<std::uint32_t*>(&ldt_desc))
                        : "r" (gdt.get_selector())
                        , "r" (ldt_selector.index)
                        : "eax", "edx");

                    ldt_selector.privilege_level = 3;
                    split_uint32_t base;
                    base.lo = ldt_desc.segment.base_lo;
                    base.hi.lo = ldt_desc.segment.base_hi_lo;
                    base.hi.hi = ldt_desc.segment.base_hi_hi;
                    split_uint32_t limit { };
                    limit.lo = ldt_desc.segment.limit_lo;
                    limit.hi = ldt_desc.segment.limit_hi;
                    ldt = linear_memory { base, limit };

                    have_access = yes;
                }
                catch (const cpu_exception&) { throw; }
                catch (const dpmi_error&) { throw; }
            }
            return have_access == yes;
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

        descriptor descriptor::clone_segment(selector s)
        {
            descriptor ldt { s };
            ldt.allocate();
            ldt.segment.privilege_level = 3;
            ldt.write();
            return ldt;
        }

        descriptor descriptor::create_segment(std::uintptr_t linear_base, std::size_t limit)
        {
            descriptor ldt = clone_segment(get_ds());
            ldt.allocate();
            ldt.set_base(linear_base);
            ldt.set_limit(limit);
            ldt.read();
            ldt.segment.is_present = true;
            ldt.write();
            return ldt;
        }

        descriptor descriptor::create_alias(selector s)
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
            descriptor ldt { new_sel };
            ldt.no_alloc = false;
            return ldt;
        }

        descriptor descriptor::create_call_gate(selector code_seg, std::uintptr_t entry_point)
        {
            split_uint32_t entry { reinterpret_cast<std::uintptr_t>(entry_point) };
            descriptor ldt { };
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

        descriptor::~descriptor()
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
        }

        void descriptor::read() const
        {
            if (direct_ldt_access())
            {
                selector_bits s { sel };
                auto& table = s.local ? ldt : gdt;
                auto* p = table.get_ptr<descriptor_data>() + s.index;
                *static_cast<descriptor_data*>(const_cast<descriptor*>(this)) = *p;
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
                    : "a" (0x000b)
                    , "b" (sel)
                    , "D" (static_cast<const descriptor_data*>(this))
                    : "memory");
                if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
            }
        }

        void descriptor::write()
        {
            if (direct_ldt_access())
            {
                selector_bits s { sel };
                auto& table = s.local ? ldt : gdt;
                auto* p = table.get_ptr<descriptor_data>() + s.index;
                *p = *static_cast<descriptor_data*>(this);
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

        std::size_t descriptor::get_limit(selector sel)
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

