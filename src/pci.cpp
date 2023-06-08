#/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
#/*    Copyright (C) 2017 - 2023 J.W. Jagersma, see COPYING.txt for details    */

#include <jw/io/pci.h>
#include <jw/dpmi/realmode.h>
#include <map>

namespace jw::io
{
    using map_type = std::map<std::uint16_t, std::map<std::uint16_t, std::map<std::uint16_t, const pci_device*>>>;
    // map indexed by: bus->device->function
    static constinit std::unique_ptr<map_type> device_map;

    static void init()
    {
        dpmi::realmode_registers reg { };
        reg.ax = 0xb101;
        reg.edi = 0;
        reg.call_int(0x1a);
        if (reg.flags.carry or reg.ah != 0 or reg.edx != 0x20494350) throw pci_device::unsupported_function { "PCI BIOS not detected." };
        if (not device_map) device_map.reset(new map_type);
    }

    pci_device::pci_device(device_tag, std::uint16_t vendor, std::initializer_list<std::uint16_t> devices, std::uint8_t function_id)
    {
        init();
        dpmi::realmode_registers reg { };
        for (auto d : devices)
        {
            for (auto i = 0;; ++i)
            {
                reg.ax = 0xb102;
                reg.cx = d;
                reg.dx = vendor;
                reg.si = i;
                reg.call_int(0x1a);
                if (reg.ah == 0x81) throw unsupported_function { "Function \"find PCI device\" not supported." };
                if (reg.ah == 0x83) throw device_not_found { "Bad vendor ID." };
                if (reg.ah == 0x86) break;
                if (reg.flags.carry) throw error { "Unknown PCI BIOS error." };
                bus = reg.bh;
                device = reg.bl >> 3;
                function = reg.bl & 0b111;
                if (function_id != 0xff && function != function_id) continue;
                if (auto b = device_map->find(bus); b != device_map->end())
                    if (auto d = b->second.find(device); d != b->second.end())
                        if (auto f = d->second.find(function); f != d->second.end())
                            continue;
                (*device_map)[bus][device][function] = this;
                return;
            }
        }
        throw device_not_found { "PCI Device not found." };
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
            for (auto i = 0; ; ++i)
            {
                dpmi::realmode_registers reg { };
                reg.ax = 0xb103;
                reg.si = i;
                reg.ecx = ecx.value;
                reg.call_int(0x1a);
                if (reg.ah == 0x86) break;
                if (reg.flags.carry) throw error { "Unknown PCI BIOS error." };
                bus = reg.bh;
                device = reg.bl >> 3;
                function = reg.bl & 0b111;
                if (auto b = device_map->find(bus); b != device_map->end())
                    if (auto d = b->second.find(device); d != b->second.end())
                        if (auto f = d->second.find(function); f != d->second.end())
                            continue;
                (*device_map)[bus][device][function] = this;
                return;
            }
        }
        throw device_not_found { "PCI Device not found." };
    }

    pci_device::~pci_device()
    {
        (*device_map)[bus][device].erase(function);
        if ((*device_map)[bus][device].empty()) (*device_map)[bus].erase(device);
        if ((*device_map)[bus].empty()) device_map->erase(bus);
        if (device_map->empty()) device_map.reset();
    }
}
