/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/dpmi/memory.h>

namespace jw::io
{
    // Allocates a buffer in conventional memory that does not cross any 64K
    // alignment boundaries, making it suitable for ISA DMA transfers.
    struct dma_buffer
    {
        dma_buffer(std::size_t size)
            : mem(size + 64_KB)
        {
            if (size > 64_KB) throw std::length_error { "DMA buffer too large" };
            const std::uintptr_t address = mem.dos_pointer().segment << 4;
            const std::uintptr_t aligned = (address + 0xffff) & 0xffff0000;
            offset = aligned - address;
            if (offset >= size) offset = 0;
            if (offset < 64_KB) mem.resize(offset + size);
        }

        std::byte* pointer() const noexcept { return mem.near_pointer() + offset; }
        std::uintptr_t physical_address() const noexcept { return (mem.dos_pointer().segment << 4) + offset; }
        std::size_t size() const noexcept { return mem.size() - offset; }

    private:
        dpmi::dos_memory<std::byte> mem;
        std::size_t offset;
    };
}
