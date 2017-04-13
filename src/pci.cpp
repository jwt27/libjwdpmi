/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <jw/io/pci.h>
#include <jw/dpmi/realmode.h>

namespace jw
{
    namespace io
    {
        pci_device::map_type* pci_device::device_map;

        void pci_device::init()
        {
            dpmi::realmode_registers reg { };
            reg.ax = 0xb101;
            reg.edi = 0;
            reg.call_int(0x1a);
            if (reg.flags.carry || reg.ah != 0 || reg.edx != 0x20494350) throw unsupported_function { "PCI BIOS not detected." };
            if (device_map == nullptr) device_map = new map_type { };
        }

        pci_device::pci_device(device_tag, std::uint16_t vendor, std::initializer_list<std::uint16_t> devices, std::uint8_t function_id)
        {
            init();
            for (auto d : devices)
            {
                for (auto i = 0;; ++i)
                {
                    dpmi::realmode_registers reg { };
                    reg.ax = 0xb102;
                    reg.cx = d;
                    reg.dx = vendor;
                    reg.si = i;
                    reg.call_int(0x1a);
                    if (reg.ah == 0x81) throw unsupported_function { "Function \"find PCI device\" not supported." };
                    if (reg.ah == 0x86) throw device_not_found { "PCI Device not found." };
                    if (reg.ah == 0x83) throw device_not_found { "Bad vendor ID." };
                    if (reg.flags.carry) throw error { "Unknown PCI BIOS error." };
                    bus = reg.bh;
                    device = reg.bl >> 3;
                    function = reg.bl & 0b111;
                    if (function_id != 0xff && function != function_id) continue;
                    if ((*device_map).count(bus) &&
                        (*device_map)[bus].count(device) &&
                        (*device_map)[bus][device].count(function)) continue;
                    (*device_map)[bus][device][function] = this;
                    return;
                }
            }
        }

        pci_device::pci_device(class_tag, std::uint8_t class_code, std::initializer_list<std::uint8_t> subclass_codes, std::uint8_t interface_type)
        {
            init();

            union
            {
                struct [[gnu::packed]]
                {
                    unsigned prog_if : 8;
                    unsigned subclass_c : 8;
                    unsigned class_c : 8;
                    unsigned : 8;
                };
                const std::uint32_t value { };
            } ecx;
            ecx.prog_if = interface_type;
            ecx.class_c = class_code;

            for (auto c : subclass_codes)
            {
                ecx.subclass_c = c;
                for (auto i = 0;; ++i)
                {
                    dpmi::realmode_registers reg { };
                    reg.ax = 0xb103;
                    reg.si = i;
                    reg.ecx = ecx.value;
                    reg.call_int(0x1a);
                    if (reg.ah == 0x86) throw device_not_found { "PCI Device not found." };
                    if (reg.flags.carry) throw error { "Unknown PCI BIOS error." };
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
