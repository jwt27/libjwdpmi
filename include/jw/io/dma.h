/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/dpmi/memory.h>
#include <jw/io/ioport.h>

namespace jw::io
{
    // Allocates a buffer in conventional memory that does not cross any 64K
    // alignment boundaries, making it suitable for ISA DMA transfers.
    template<typename T>
    struct dma_buffer
    {
        dma_buffer(std::size_t num_elements)
            : mem(sizeof(T) * num_elements * 2)
        {
            const std::size_t n = sizeof(T) * num_elements;
            if (n > 64_KB) throw std::length_error { "DMA buffer too large" };
            const std::uintptr_t address = mem.dos_pointer().segment << 4;
            const std::uintptr_t aligned = (address + 0xffff) & 0xffff0000;
            offset = aligned - address;
            if (offset >= n) offset = 0;
            if (offset < n) mem.resize(offset + n);
        }

        T* pointer() const noexcept { return reinterpret_cast<T*>(mem.near_pointer() + offset); }
        std::uintptr_t physical_address() const noexcept { return (mem.dos_pointer().segment << 4) + offset; }
        std::size_t size_bytes() const noexcept { return mem.size() - offset; }
        std::size_t size() const noexcept { return size_bytes() / sizeof(T); }

    private:
        dpmi::dos_memory<std::byte> mem;
        std::size_t offset;
    };

    enum class dma_mode : unsigned
    {
        on_demand = 0b0000,
        single = 0b0100,
        block = 0b1000,
        auto_on_demand = 0b0001,
        auto_single = 0b0101,
        auto_block = 0b1001
    };

    enum class dma_direction : unsigned
    {
        from_device = 0b01,
        to_device = 0b10
    };

    // ISA DMA channel implementation.  Don't use this directly, prefer
    // dma8_channel and dma16_channel defined below.
    template<bool high>
    struct dma_channel_impl
    {
        dma_channel_impl(unsigned c)
            : ch { high ? c >> 2 : c }
        {
            if (high and c > 4 and c < 8) return;
            else if (not high and c < 4) return;
            throw std::invalid_argument { "Invalid DMA channel" };
        }

        ~dma_channel_impl() { disable(); }

        dma_channel_impl(dma_channel_impl&&) = delete;
        dma_channel_impl(const dma_channel_impl&) = delete;
        dma_channel_impl& operator=(dma_channel_impl&&) = delete;
        dma_channel_impl& operator=(const dma_channel_impl&) = delete;

        // Returns the assigned DMA channel number.
        unsigned channel() const noexcept { assume(ch < 4); return high ? ch << 2 : ch; }

        // Unmask the DMA request line for this channel.
        void enable() noexcept { mask_port().write(ch); }

        // Mask requests on this DMA channel.  Make sure to call this, and
        // disable interrupts, before calling any of the functions below.
        void disable() noexcept { mask_port().write(ch | 4); }

        // Set the DMA transfer mode and direction.
        void set_mode(dma_mode m, dma_direction dir) noexcept
        {
            assume(ch < 4);
            mode_port().write(mode_register { ch, dir, m });
        }

        // Set the start address for the DMA transaction.  This must be a
        // physical address residing below 16MB.  The whole buffer may not
        // cross any physical 64KB boundaries.  For 16-bit transfers, the
        // start address must be aligned to a two-byte boundary.
        void set_address(std::uintptr_t physical_address) noexcept
        {
            reset_flipflop();
            do_set_address(physical_address);
        }

        // Set the DMA buffer size.  This is the number of transfers to be
        // made - for 16-bit channels, each count transfers two bytes.
        void set_count(std::uint16_t count) noexcept
        {
            reset_flipflop();
            do_set_count(count);
        }

        // Initiate a DMA transfer. This simply sets the address, count, and
        // mode in one step.  To restart the same transaction, only the count
        // register needs to be set.
        void transfer(std::uintptr_t physical_address, std::uint16_t count, dma_mode m, dma_direction dir) noexcept
        {
            set_mode(m, dir);
            reset_flipflop();
            do_set_address(physical_address);
            do_set_count(count);
        }

        // Initiate a DMA transfer using the given buffer.
        template<typename T>
        void transfer(const dma_buffer<T>& buf, dma_mode m, dma_direction dir) noexcept
        {
            const auto n = buf.size_bytes();
            return transfer(buf.physical_address(), high ? n / 2 : n, m, dir);
        }

    private:
        struct [[gnu::packed]] mode_register
        {
            unsigned channel : 2;
            dma_direction direction : 2;
            dma_mode mode : 4;
        };
        static_assert(sizeof(mode_register) == 1);

        static void reset_flipflop() noexcept { asm volatile ("out %0, al" :: "N" (high ? 0xd8 : 0x0c)); }

        static constexpr out_port<mode_register> mode_port() noexcept { return { high ? 0xd6 : 0x0b }; };
        static constexpr out_port<std::uint8_t>  mask_port() noexcept { return { high ? 0xd4 : 0x0a }; };

        out_port<std::uint8_t> address_port() const noexcept { assume(ch < 4); return { static_cast<port_num>(high ? 0xc0 + (ch << 2) : 0x00 + (ch << 1)) }; };
        out_port<std::uint8_t> count_port()   const noexcept { assume(ch < 4); return { static_cast<port_num>(high ? 0xc2 + (ch << 2) : 0x01 + (ch << 1)) }; };

        out_port<std::uint8_t> page_port() const noexcept { assume(ch < 4); return { static_cast<port_num>(((high ? 0x8a898b8fu : 0x82818387u) >> (ch << 3)) & 0xff) }; }

        void do_set_address(std::uintptr_t physical_address) noexcept
        {
            const auto port = address_port();
            split_uint32_t a { physical_address };
            if constexpr (high) a.lo = a.lo >> 1;
            page_port().write(a.hi.lo);
            port.write(a.lo.lo);
            port.write(a.lo.hi);
        }

        void do_set_count(std::uint16_t count) noexcept
        {
            const auto port = count_port();
            split_uint16_t n { count - 1 };
            port.write(n.lo);
            port.write(n.hi);
        }

        const unsigned ch;
    };

    // 8-bit DMA channel (0 to 3)
    using dma8_channel = dma_channel_impl<false>;

    // 16-bit DMA channel (5 to 7)
    using dma16_channel = dma_channel_impl<true>;
}
