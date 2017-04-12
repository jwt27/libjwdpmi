/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <initializer_list>
#include <unordered_map>
#include <jw/dpmi/realmode.h>
#include <jw/common.h>

namespace jw
{
    namespace io
    {
        struct pci_device
        {
            struct error : public std::runtime_error { using runtime_error::runtime_error; };
            struct unsupported_function : public error { using error::error; };
            struct bad_register : public error { using error::error; };
            struct device_not_found : public error { using error::error; };

        protected:
            pci_device(std::uint16_t vendor, std::initializer_list<std::uint16_t> devices, std::uint8_t function = 0xff);
            virtual ~pci_device();

            template<typename T, std::uint8_t reg>
            struct pci_register
            {
                constexpr pci_register(pci_device* device) : dev(*device) { }

                auto read()
                {

                }

                void write(T value)
                {

                }

            private:
                pci_device& dev { };
            };

        private:
            std::uint16_t vendor_id;
            std::uint16_t device_id;
            std::uint8_t bus;
            std::uint8_t bus_device;
            std::uint16_t index;
            std::uint8_t function_id;

            // map indexed by: vendor->device->index
            using map_type = std::unordered_map<std::uint16_t, std::unordered_map<std::uint16_t, std::unordered_map<std::uint16_t, pci_device*>>>;
            static map_type* device_map;
        };
    }
}
