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
                static_assert(sizeof(T) == 4 && (reg % 4) == 0, "PCI registers must be 4 bytes wide.");
                constexpr pci_register(pci_device* device) : dev(*device) { }

                auto read()
                {
                    index.write(get_regnum());
                    return data.read();
                }

                void write(const T& value)
                {
                    index.write(get_regnum());
                    data.write(value);
                }

            private:
                std::uint32_t get_regnum()
                {
                    union
                    {
                        struct
                        {
                            unsigned register_num : 8;
                            unsigned function : 3;
                            unsigned device : 5;
                            unsigned bus : 8;
                            unsigned : 7;
                            bool enable_config : 1;
                        };
                        std::uint32_t value { };
                    } x;
                    x.register_num = reg;
                    x.function = dev.function;
                    x.device = dev.bus_device;
                    x.bus = dev.bus;
                    x.enable_config = true;
                    return x.value;
                }

                pci_device& dev { };
                static constexpr out_port<std::uint32_t> index { 0xcf8 };
                static constexpr io_port<T> data { 0xcfc };
            };

            struct id_reg
            {
                std::uint16_t device, vendor;
            };

            pci_register<id_reg, 0> id { this };

        private:
            std::uint16_t device_id, vendor_id;
            std::uint8_t bus, bus_device;
            std::uint16_t index;
            std::uint8_t function;

            using map_type = std::unordered_map<std::uint16_t, std::unordered_map<std::uint16_t, std::unordered_map<std::uint16_t, pci_device*>>>;
            // map indexed by: vendor->device->index
            static map_type* device_map;
        };
    }
}
