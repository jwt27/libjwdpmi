/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#include <optional>
#include <jw/dpmi/memory.h>
#include <jw/dpmi/cpu_exception.h>
#include <jw/dpmi/ring0.h>

namespace jw::dpmi
{
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
                [[maybe_unused]] volatile descriptor_data data = test.read();
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

    descriptor::descriptor(descriptor&& d) noexcept
        : sel(d.sel), no_alloc(d.no_alloc)
    {
        d.no_alloc = true;
    }

    descriptor& descriptor::operator=(descriptor&& d)
    {
        deallocate();
        new(this) descriptor(std::move(d));
        return *this;
    }

    descriptor descriptor::create()
    {
        descriptor d;
        d.allocate();
        return d;
    }

    descriptor descriptor::create_segment(std::uintptr_t linear_base, std::size_t limit)
    {
        descriptor ldt = clone_segment(detail::main_ds);
        ldt.set_base(linear_base);
        ldt.set_limit(limit);
        return ldt;
    }

    descriptor descriptor::create_code_segment(std::uintptr_t linear_base, std::size_t limit)
    {
        descriptor ldt = clone_segment(detail::main_cs);
        ldt.set_base(linear_base);
        ldt.set_limit(limit);
        return ldt;
    }

    descriptor descriptor::clone_segment(selector s)
    {
        descriptor d { s };
        auto data = d.read();
        d.allocate();
        d.write(data);
        return d;
    }

    descriptor descriptor::create_call_gate(selector code_seg, std::uintptr_t entry_point)
    {
        split_uint32_t entry { entry_point };
        descriptor_data data { };
        auto& c = data.call_gate;
        c.not_system_segment = false;
        c.privilege_level = 3;
        c.type = call_gate32;
        c.is_present = true;
        c.cs = code_seg;
        c.offset_lo = entry.lo;
        c.offset_hi = entry.hi;
        c.stack_params = 0;

        descriptor d { };
        d.allocate();
        d.write(data);
        return d;
    }

    descriptor::~descriptor()
    {
        try { deallocate(); }
        catch(...) { }
    }

    descriptor_data descriptor::read() const
    {
        auto ldt_access = direct_ldt_access();
        if (ldt_access != no) [[likely]]
            return read_descriptor_direct(sel, ldt_access == ring0);

        descriptor_data data;
        dpmi_error_code error;
        bool c;
        asm volatile(
            "lea edi, %2;"
            "int 0x31;"
            : "=@ccc" (c)
            , "=a" (error)
            , "=m" (data)
            : "a" (0x000b)
            , "b" (sel)
            : "edi", "memory");
        if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
        return data;
    }

    void descriptor::write(const descriptor_data& data)
    {
        auto ldt_access = direct_ldt_access();
        if (ldt_access != no) [[likely]]
            write_descriptor_direct(sel, data, ldt_access == ring0);

        dpmi_error_code error;
        bool c;
        asm volatile(
            "int 0x31"
            : "=@ccc" (c)
            , "=a" (error)
            : "a" (0x000c)
            , "b" (sel)
            , "D" (&data));
        if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
    }

    std::uintptr_t descriptor::get_base(selector seg)
    {
        dpmi_error_code error;
        split_uint32_t base;
        bool c;

        asm("int 0x31"
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
            "int 0x31"
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
        auto data = read();
        auto v { data.segment.limit() };
        if (data.segment.is_page_granular) return v << 12 | ((1 << 12) - 1);
        return v;
    }

    std::size_t descriptor::get_limit(selector sel)
    {
        if (selector_bits { sel }.privilege_level < selector_bits { get_cs() }.privilege_level)
            return descriptor { sel }.get_limit();

        std::size_t limit;
        bool z;
        asm("lsl %1, %2"
            : "=@ccz" (z)
            , "=r" (limit)
            : "rm" (static_cast<std::uint32_t>(sel))
            : "cc");
        if (not z) throw dpmi_error { invalid_selector, __PRETTY_FUNCTION__ };
        return limit;
    }

    void descriptor::set_limit(selector sel, std::size_t limit)
    {
        dpmi_error_code error;
        split_uint32_t _limit = (limit >= 1_MB) ? round_up_to_page_size(limit) - 1 : limit;
        bool c;

        asm volatile(
            "int 0x31"
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
            "int 0x31"
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
            "int 0x31"
            : "=@ccc" (c)
            , "=a" (error)
            : "a" (0x0001)
            , "b" (sel));
        if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
        no_alloc = true;
    }

    dos_alloc_result dos_allocate(std::size_t num_bytes)
    {
        throw_if_irq();
        if (num_bytes > 0xffff0) throw std::invalid_argument { "Allocation exceeds 1MB" };
        std::uint16_t ax = 0x0100;
        std::uint16_t bx = bytes_to_paragraphs(num_bytes);
        std::uint16_t dx;
        bool c;
        asm
        (
            "int 0x31"
            : "=@ccc" (c), "+a" (ax), "+b" (bx), "=d" (dx)
            :
            :
        );
        if (c) [[unlikely]]
        {
            if (ax == dpmi_error_code::insufficient_memory)
                throw bad_dos_alloc { static_cast<std::size_t>(bx) << 4 };
            else
                throw dpmi_error { ax, __PRETTY_FUNCTION__ };
        }
        return  { { ax, 0 }, dx };
    }

    void dos_resize(selector s, std::size_t num_bytes)
    {
        throw_if_irq();
        if (num_bytes > 0xffff0) throw std::invalid_argument { "Allocation exceeds 1MB" };
        std::uint16_t ax = 0x0102;
        std::uint16_t bx = bytes_to_paragraphs(num_bytes);
        bool c;
        asm
        (
            "int 0x31"
            : "=@ccc" (c), "+a" (ax), "+b" (bx)
            : "d" (s)
            :
        );
        if (c) [[unlikely]]
        {
            if (ax == dpmi_error_code::insufficient_memory)
                throw bad_dos_alloc { static_cast<std::size_t>(bx) << 4 };
            else
                throw dpmi_error { ax, __PRETTY_FUNCTION__ };
        }
    }

    void dos_free(selector s)
    {
        throw_if_irq();
        std::uint16_t ax = 0x0101;
        bool c;
        asm volatile
        (
            "int 0x31"
            : "=@ccc" (c), "+a" (ax)
            : "d" (s)
            :
        );
        if (c) throw dpmi_error { ax, __PRETTY_FUNCTION__ };
    }

    selector dos_selector(std::uint16_t segment)
    {
        std::uint16_t ax = 0x0002;
        bool c;
        asm
        (
            "int 0x31"
            : "=@ccc" (c), "+a" (ax)
            : "b" (segment)
            :
        );
        if (c) throw dpmi_error(ax, __PRETTY_FUNCTION__);
        return ax;
    }

    static bool is_valid_address(std::uintptr_t base, std::size_t limit)
    {
        // Discard blocks below base address.
        if (base <= static_cast<std::uintptr_t>(__djgpp_base_address)) return false;

        // Bump selector limit.
        const std::size_t new_limit = round_up_to_page_size(base + limit) - 1;
        if (static_cast<std::size_t>(__djgpp_selector_limit) < new_limit)
        {
            __djgpp_selector_limit = new_limit;
            descriptor::set_limit(detail::safe_ds, new_limit);
            descriptor::set_limit(detail::main_cs, new_limit);
            if (descriptor::get_limit(detail::main_ds) != 0xfff)
                descriptor::set_limit(detail::main_ds, new_limit);
        }
        return true;
    }

    void memory_base::dpmi09_alloc()
    {
        throw_if_irq();
        if (handle != null_handle) deallocate();
        split_uint32_t new_size { size() };
        split_uint32_t new_addr, new_handle;
        std::uint16_t ax { 0x0501 };
        bool c;
        do
        {
            asm volatile
            (
                "int 0x31"
                : "=@ccc" (c)
                , "+a" (ax)
                , "=b" (new_addr.hi)
                , "=c" (new_addr.lo)
                , "=S" (new_handle.hi)
                , "=D" (new_handle.lo)
                : "b" (new_size.hi)
                , "c" (new_size.lo)
                :
            );
            if (c) throw dpmi_error { ax, __PRETTY_FUNCTION__ };
        } while (not is_valid_address(new_addr, size()));
        handle = new_handle;
        addr = new_addr;
    }

    std::optional<dpmi_error> memory_base::dpmi10_alloc(bool committed, std::uintptr_t desired_address)
    {
        if (committed) throw_if_irq();
        if (handle != null_handle) deallocate();
        std::uint32_t new_handle;
        std::uintptr_t new_addr;
        std::uint16_t ax { 0x0504 };
        bool c;
        do
        {
            asm volatile
            (
                "int 0x31"
                : "=@ccc" (c)
                , "+a" (ax)
                , "=b" (new_addr)
                , "=S" (new_handle)
                : "b" (desired_address)
                , "c" (bytes)
                , "d" (static_cast<std::uint32_t>(committed))
                :
            );
            if (c) return dpmi_error { ax, __PRETTY_FUNCTION__ };
        } while (not is_valid_address(new_addr, size()));
        handle = new_handle;
        addr = new_addr;
        return std::nullopt;
    }

    void memory_base::dpmi09_resize(std::size_t num_bytes)
    {
        throw_if_irq();
        split_uint32_t new_size { num_bytes };
        split_uint32_t new_handle { handle };
        split_uint32_t new_addr;
        std::uint16_t ax { 0x0503 };
        bool c;
        do
        {
            asm volatile
            (
                "int 0x31"
                : "=@ccc" (c)
                , "+a" (ax)
                , "=b" (new_addr.hi)
                , "=c" (new_addr.lo)
                , "+S" (new_handle.hi)
                , "+D" (new_handle.lo)
                : "b" (new_size.hi)
                , "c" (new_size.lo)
                :
            );
            if (c) throw dpmi_error { ax, __PRETTY_FUNCTION__ };
        } while (not is_valid_address(new_addr, num_bytes));
        handle = new_handle;
        addr = new_addr;
        bytes = new_size;
    }

    void memory_base::dpmi10_resize(std::size_t num_bytes, bool committed)
    {
        if (committed) throw_if_irq();
        std::uint32_t new_handle { handle };
        std::uintptr_t new_addr;
        std::uint16_t ax { 0x0505 };
        bool c;
        do
        {
            asm volatile
            (
                "int 0x31"
                : "=@ccc" (c)
                , "+a" (ax)
                , "=b" (new_addr)
                , "+S" (new_handle)
                : "c" (num_bytes)
                , "d" (static_cast<std::uint32_t>(committed))
                :
            );
            if (c) throw dpmi_error { ax, __PRETTY_FUNCTION__ };
        } while (not is_valid_address(new_addr, num_bytes));
        handle = new_handle;
        addr = new_addr;
        bytes = num_bytes;
    }

    void device_memory_base::dpmi09_alloc(std::uintptr_t physical_address)
    {
        split_uint32_t new_addr;
        split_uint32_t new_size { size() };
        split_uint32_t phys { physical_address };
        std::uint16_t ax { 0x0800 };
        bool c;
        asm volatile
        (
            "int 0x31"
            : "=@ccc" (c)
            , "+a" (ax)
            , "=b" (new_addr.hi)
            , "=c" (new_addr.lo)
            : "b" (phys.hi)
            , "c" (phys.lo)
            , "S" (new_size.hi)
            , "D" (new_size.lo)
            :
        );
        if (c) throw dpmi_error { ax, __PRETTY_FUNCTION__ };
        addr = new_addr;
    }

    void device_memory_base::dpmi10_alloc(std::uintptr_t physical_address)
    {
        auto addr_start = round_down_to_page_size(physical_address);
        auto offset = physical_address - addr_start;
        auto pages = round_up_to_page_size(size()) / page_size;
        auto offset_in_block = round_up_to_page_size(addr) - addr;
        offset += offset_in_block;
        addr += offset;
        bytes -= offset;
        std::uint16_t ax { 0x0508 };
        bool c;
        asm volatile
        (
            "int 0x31"
            : "=@ccc" (c)
            , "+a" (ax)
            : "b" (offset_in_block)
            , "c" (pages)
            , "d" (addr_start)
            , "S" (handle)
            : "memory"
        );
        if (c) throw dpmi_error { ax, __PRETTY_FUNCTION__ };
    }

    void mapped_dos_memory_base::alloc(std::uintptr_t dos_physical_address)
    {
        auto addr_start = round_down_to_page_size(dos_physical_address);
        offset = dos_physical_address - addr_start;
        auto pages = round_up_to_page_size(size()) / page_size;
        auto offset_in_block = round_up_to_page_size(addr) - addr;
        addr += offset + offset_in_block;
        bytes -= offset + offset_in_block;
        std::uint16_t ax { 0x0509 };
        bool c;
        asm volatile
        (
            "int 0x31"
            : "=@ccc" (c)
            , "+a" (ax)
            : "b" (offset_in_block)
            , "c" (pages)
            , "d" (addr_start)
            , "S" (handle)
            : "memory"
        );
        if (c) throw dpmi_error { ax, __PRETTY_FUNCTION__ };
    }
}
