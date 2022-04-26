/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/dpmi/memory.h>

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
}
