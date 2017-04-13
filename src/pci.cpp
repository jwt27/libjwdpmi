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

            vendor_id = vendor;
            for (auto device : devices)
            {
                device_id = device;
                for (auto i = 0;; ++i)
                {
                    if ((*device_map)[vendor_id][device_id].count(i)) continue;
                    reg = { };
                    reg.ax = 0xb102;
                    reg.cx = device_id;
                    reg.dx = vendor_id;
                    reg.si = i;
                    reg.call_int(0x1a);
                    if (reg.ah == 0x81) throw unsupported_function { "Function \"find PCI device\" not supported." };
                    if (reg.ah == 0x86) throw device_not_found { "PCI Device not found." };
                    if (reg.ah == 0x83) throw device_not_found { "Bad vendor ID." };
                    if (reg.flags.carry) throw error { "Unknown PCI BIOS error." };
                    if (function_id != 0xff && (reg.bl & 0b111) != function_id) continue;
                    bus = reg.bh;
                    bus_device = reg.bl >> 3;
                    function = reg.bl & 0b111;
                    (*device_map)[vendor_id][device_id][index] = this;
                    return;
                }
            }
        }

        pci_device::~pci_device()
        {
            device_map[vendor_id][device_id].erase(index);
            if ((*device_map)[vendor_id][device_id].empty()) device_map[vendor_id].erase(device_id);
            if ((*device_map)[vendor_id].empty()) device_map->erase(vendor_id);
            if (device_map->empty()) delete device_map;
        }
    }
}
