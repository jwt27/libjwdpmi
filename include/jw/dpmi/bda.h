/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2022 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <type_traits>
#include <cstddef>
#include <array>

namespace jw::dpmi
{
    struct bios_data_area
    {
        std::array<std::byte, 0x100> bytes;

        template<typename T>
        volatile T& ref(std::size_t offset)
        {
            return *reinterpret_cast<T*>(bytes.begin() + offset);
        }

        template<typename T>
        T read(std::size_t offset)
        {
            return *reinterpret_cast<volatile T*>(bytes.begin() + offset);
        }

        template<typename T>
        void write(std::size_t offset, const std::type_identity_t<T>& value)
        {
            *reinterpret_cast<volatile T*>(bytes.begin() + offset) = value;
        }
    };

    extern bios_data_area* const bda;
}
