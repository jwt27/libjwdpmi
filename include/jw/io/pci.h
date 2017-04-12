/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/dpmi/realmode.h>
#include <jw/common.h>

namespace jw
{
    namespace io
    {
        struct pci_device
        {
        protected:
            pci_device(auto vendor, auto devices)
            {
                dpmi::realmode_registers reg { };
                reg.ax = 0xb101;
                reg.edi = 0;
                reg.call_int(0x1A);
                if (reg.edx != 0x20494350) throw std::runtime_error { "PCI not supported." };

            }

            template<typename T, std::uint8_t reg>
            struct pci_register
            {
                constexpr pci_register(pci_device* device) : dev(device) { }

                auto read()
                {

                }

                void write(T value)
                {

                }

            private:
                pci_device* dev { };
            };

            std::uint16_t vendor_id;
            std::uint16_t device_id;
        };
    }
}
