/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2016 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#include <optional>
#include <jw/dpmi/memory.h>
#include <jw/dpmi/cpu_exception.h>
#include <jw/dpmi/ring0.h>

namespace jw::dpmi
{
    static bool direct_ldt_access = false;
    static std::optional<descriptor> gdt, ldt;
    static finally cleanup { [] { direct_ldt_access = false; } };

    [[gnu::noipa]] static void may_throw() { }; // stupid hack

    template<bool safe = false>
    [[gnu::noipa]] static void test_descriptor_direct(selector table)
    {
        gs_override gs { table };
        std::uint32_t x;
        may_throw();
        asm volatile ("mov %0, gs:[0]" : "=r" (x));
        may_throw();
        asm volatile ("mov gs:[0], %0" :: "r" (x) : "memory");
        may_throw();
    }

    static descriptor_data read_descriptor_direct(selector_bits s)
    {
        union
        {
            split_uint64_t raw;
            descriptor_data data;
        };
        auto& table = s.local ? ldt : gdt;
        gs_override gs { table->get_selector() };
        asm ("mov %0, gs:[%1*8+0]" : "=r" (raw.lo) : "r" (s.index));
        asm ("mov %0, gs:[%1*8+4]" : "=r" (raw.hi) : "r" (s.index));
        return data;
    }

    static void write_descriptor_direct(selector_bits s, const descriptor_data& d)
    {
        union
        {
            split_uint64_t raw;
            descriptor_data data;
        };

        data = d;
        auto& table = s.local ? ldt : gdt;
        gs_override gs { table->get_selector() };
        asm ("mov gs:[%1*8+0], %0" :: "r" (raw.lo), "r" (s.index) : "memory");
        asm ("mov gs:[%1*8+4], %0" :: "r" (raw.hi), "r" (s.index) : "memory");
    }
}

namespace jw::dpmi::detail
{
    struct [[gnu::packed]] gdt_register
    {
        std::uint16_t limit;
        std::uint32_t base;
    };

    [[gnu::noipa]] static auto sgdt()
    {
        gdt_register gdtr;
        may_throw();
        asm ("sgdt %0"  : "=m"  (gdtr));
        may_throw();
        return gdtr;
    }

    [[gnu::noipa]] static auto sldt()
    {
        selector ldtr;
        may_throw();
        asm ("sldt %w0" : "=rm" (ldtr));
        may_throw();
        return ldtr;
    }

    void setup_direct_ldt_access() noexcept
    {
        try
        {
            gdt_register gdtr;
            selector ldtr;

            gdtr = sgdt();
            ldtr = sldt();

            gdt.emplace(descriptor::create_segment(gdtr.base, gdtr.limit + 1));
            test_descriptor_direct(gdt->get_selector());

            auto ldt_data = read_descriptor_direct(ldtr);
            ldt.emplace(descriptor::create_segment(ldt_data.segment.base(), ldt_data.segment.limit()));
            test_descriptor_direct(ldt->get_selector());

            direct_ldt_access = true;

            descriptor test { get_ds() };
            [[maybe_unused]] volatile descriptor_data test_data = test.read();
        }
        catch (...)
        {
            direct_ldt_access = false;
            gdt.reset();
            ldt.reset();
        }
    }
}

namespace jw::dpmi
{
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
        if (direct_ldt_access) [[likely]]
            return read_descriptor_direct(sel);

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
            , "b" (sel | 3)
            : "edi", "memory");
        if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
        return data;
    }

    void descriptor::write(const descriptor_data& data)
    {
        if (direct_ldt_access) [[likely]]
            return write_descriptor_direct(sel, data);

        dpmi_error_code error;
        bool c;
        asm volatile(
            "int 0x31"
            : "=@ccc" (c)
            , "=a" (error)
            : "a" (0x000c)
            , "b" (sel | 3)
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
            , "b" (seg | 3));
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
            , "b" (seg | 3)
            , "c" (base.hi)
            , "d" (base.lo)
            : "memory");
        if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
    }

    std::size_t descriptor::get_limit(selector sel)
    {
        if (direct_ldt_access) [[likely]]
        {
            auto data = descriptor { sel }.read();
            auto v { data.segment.limit() };
            if (data.segment.is_page_granular) return v << 12 | ((1 << 12) - 1);
            return v;
        }

        std::size_t limit;
        bool z;
        asm("lsl %1, %2"
            : "=@ccz" (z)
            , "=r" (limit)
            : "rm" (static_cast<std::uint32_t>(sel) | 3)
            : "cc");
        if (not z) throw dpmi_error { invalid_selector, __PRETTY_FUNCTION__ };
        return limit;
    }

    void descriptor::set_limit(selector sel, std::size_t limit)
    {
        if (direct_ldt_access) [[likely]]
        {
            auto d = descriptor { sel };
            auto data = d.read();
            if (limit >= 1_MB)
            {
                data.segment.is_page_granular = true;
                data.segment.limit(limit >> 12);
            }
            else
            {
                data.segment.is_page_granular = false;
                data.segment.limit(limit);
            }
            d.write(data);
            return;
        }

        dpmi_error_code error;
        split_uint32_t _limit = (limit >= 1_MB) ? round_up_to_page_size(limit) - 1 : limit;
        bool c;

        asm volatile(
            "int 0x31"
            : "=@ccc" (c)
            , "=a" (error)
            : "a" (0x0008)
            , "b" (sel | 3)
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

    void memory_base::allocate(bool committed, std::uintptr_t desired_address)
    {
        try
        {
            if (dpmi10_alloc_supported)
            {
                auto error = dpmi10_alloc(committed, desired_address);
                if (not error) return;
                switch (error->code().value())
                {
                case unsupported_function:
                case 0x0504:
                    dpmi10_alloc_supported = false;
                    break;
                default: throw* error;
                }
            }
            dpmi09_alloc();
        }
        catch (...)
        {
            std::throw_with_nested(std::bad_alloc { });
        }
    }

    void memory_base::deallocate()
    {
        if (handle == 0) return;
        split_uint32_t _handle { handle };
        std::uint16_t ax { 0x0502 };
        [[maybe_unused]] bool c;
        asm volatile
        (
            "int 0x31"
            : "=@ccc" (c), "+a" (ax)
            : "S" (_handle.hi), "D" (_handle.lo)
            :
        );
#       ifndef NDEBUG
        if (c) throw dpmi_error { ax, __PRETTY_FUNCTION__ };
#       endif
        handle = 0;
    }

    void memory_base::resize(std::size_t num_bytes, bool committed)
    {
        if (dpmi10_alloc_supported) dpmi10_resize(num_bytes, committed);
        else dpmi09_resize(num_bytes);
    }

    static bool check_base_limit(std::uintptr_t base, std::size_t limit)
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

    inline void memory_base::dpmi09_alloc()
    {
        throw_if_irq();
        if (handle != 0) deallocate();
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
        } while (not check_base_limit(new_addr, size()));
        handle = new_handle;
        addr = new_addr;
    }

    inline std::optional<dpmi_error> memory_base::dpmi10_alloc(bool committed, std::uintptr_t desired_address)
    {
        if (committed) throw_if_irq();
        if (handle != 0) deallocate();
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
        } while (not check_base_limit(new_addr, size()));
        handle = new_handle;
        addr = new_addr;
        return std::nullopt;
    }

    inline void memory_base::dpmi09_resize(std::size_t num_bytes)
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
        } while (not check_base_limit(new_addr, num_bytes));
        handle = new_handle;
        addr = new_addr;
        bytes = new_size;
    }

    inline void memory_base::dpmi10_resize(std::size_t num_bytes, bool committed)
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
        } while (not check_base_limit(new_addr, num_bytes));
        handle = new_handle;
        addr = new_addr;
        bytes = num_bytes;
    }

    void device_memory_base::allocate(std::uintptr_t physical_address, bool use_dpmi09_alloc)
    {
        if (not use_dpmi09_alloc and device_map_supported())
        {
            base::allocate(false);
            dpmi10_alloc(physical_address);
        }
        else dpmi09_alloc(physical_address);
    }

    void device_memory_base::deallocate()
    {
        if (device_map_supported())
        {
            base::deallocate();
            return;
        }
        else
        {
            split_uint32_t old_addr { addr };
            asm volatile
            (
                "int 0x31"
                :
                : "a" (0x0801)
                , "b" (old_addr.hi), "c" (old_addr.lo)
                :
            );
            // This is an optional dpmi 1.0 function.  Don't care if this fails.
        }
    }

    inline void device_memory_base::dpmi09_alloc(std::uintptr_t physical_address)
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
        check_base_limit(addr, bytes);
    }

    inline void device_memory_base::dpmi10_alloc(std::uintptr_t physical_address)
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

    static bool dos_map_supported()
    {
        static bool sup = []
        {
            capabilities c { };
            return c.supported and c.flags.conventional_memory_mapping;
        }();
        return sup;
    }

    void mapped_dos_memory_base::allocate(std::uintptr_t dos_physical_address)
    {
        if (not dos_map_supported())
            throw dpmi_error { unsupported_function, __PRETTY_FUNCTION__ };

        base::allocate(false);
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
        return;
    }

    void dos_memory_base::resize(std::size_t num_bytes, bool)
    {
        num_bytes = round_up_to_paragraph_size(num_bytes);
        const bool remap = num_bytes > bytes;
        if (remap) base::deallocate();
        dos_resize(dos_handle, num_bytes);
        bytes = num_bytes;
        [[assume(dos_addr.offset == 0)]];
        if (remap) base::allocate(conventional_to_physical(dos_addr));
    }

    void dos_memory_base::allocate()
    {
        deallocate();
        auto result = dpmi::dos_allocate(bytes);
        dos_handle = result.handle;
        dos_addr = result.pointer;
        [[assume(dos_addr.offset == 0)]];
        base::allocate(conventional_to_physical(dos_addr));
    }

    void dos_memory_base::deallocate()
    {
        base::deallocate();
        if (dos_handle == 0) return;
        dos_free(dos_handle);
        dos_handle = 0;
    }
}
