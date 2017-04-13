/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <jw/io/pci.h>

namespace jw
{
    namespace io
    {
        pci_device::map_type* pci_device::device_map;

        pci_device::pci_device(std::uint16_t vendor, std::initializer_list<std::uint16_t> devices, std::uint8_t function_id)
        {
            dpmi::realmode_registers reg { };
            reg.ax = 0xb101;
            reg.edi = 0;
            reg.call_int(0x1a);
            if (reg.flags.carry || reg.ah != 0 || reg.edx != 0x20494350) throw unsupported_function { "PCI BIOS not detected." };
            if (device_map == nullptr) device_map = new map_type { };

            for (auto d : devices)
            {
                for (auto i = 0;; ++i)
                {
                    reg = { };
                    reg.ax = 0xb102;
                    reg.cx = d;
                    reg.dx = vendor;
                    reg.si = i;
                    reg.call_int(0x1a);
                    if (reg.ah == 0x81) throw unsupported_function { "Function \"find PCI device\" not supported." };
                    if (reg.ah == 0x86) throw device_not_found { "PCI Device not found." };
                    if (reg.ah == 0x83) throw device_not_found { "Bad vendor ID." };
                    if (reg.flags.carry) throw error { "Unknown PCI BIOS error." };
                    if (function_id != 0xff && (reg.bl & 0b111) != function_id) continue;
                    bus = reg.bh;
                    device = reg.bl >> 3;
                    function = reg.bl & 0b111;
                    if ((*device_map).count(bus) &&
                        (*device_map)[bus].count(device) &&
                        (*device_map)[bus][device].count(function)) continue;
                    (*device_map)[bus][device][function] = this;
                    return;
                }
            }
        }

        pci_device::~pci_device()
        {
            device_map[bus][device].erase(function);
            if ((*device_map)[bus][device].empty()) device_map[bus].erase(device);
            if ((*device_map)[bus].empty()) device_map->erase(device);
            if (device_map->empty()) delete device_map;
        }
    }
}
