/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <jw/video/vbe.h>

namespace jw
{
    namespace video
    {
        const vbe_info& vbe2::get_vbe_info()
        {
            if (info.vbe_signature == "VESA") return info;

            dpmi::dos_memory<raw_vbe_info> raw_info { 1 };
            auto* ptr = raw_info.get_ptr();
            std::copy_n("VBE2", 4, ptr->vbe_signature);

            dpmi::rm_registers reg { };
            reg.ax = 0x4f00;
            reg.es = raw_info.get_dos_ptr().segment;
            reg.di = raw_info.get_dos_ptr().offset;
            reg.call_rm_interrupt(0x10);
            // TODO: check ax for return value
            if (strncmp(ptr->vbe_signature, "VESA", 4) != 0) throw std::runtime_error { "VBE2 not supported." };

            info.vbe_signature.assign(ptr->vbe_signature, ptr->vbe_signature + 4);
            info.vbe_version = ptr->vbe_version;

            std::copy_n(&ptr->capabilities, 1, reinterpret_cast<std::uint32_t*>(&info.capabilities));
            info.total_memory = ptr->total_memory;
            info.oem_software_ver = ptr->oem_software_ver;
            std::copy_n(ptr->oem_data, 256, info.oem_data.data());
            {
                dpmi::mapped_dos_memory<char> str { 256, ptr->oem_string };
                info.oem_string = str.get_ptr();
            }
            {
                dpmi::mapped_dos_memory<char> str { 256, ptr->oem_vendor_name };
                info.oem_vendor_name = str.get_ptr();
            }
            {
                dpmi::mapped_dos_memory<char> str { 256, ptr->oem_product_name };
                info.oem_product_name = str.get_ptr();
            }
            {
                dpmi::mapped_dos_memory<char> str { 256, ptr->oem_product_version };
                info.oem_product_version = str.get_ptr();
            }

            dpmi::mapped_dos_memory<std::uint16_t> mode_list { 256, ptr->video_mode_list };
            dpmi::dos_memory<vbe_mode_info> mode_info { 1 };
            for (auto* mode_ptr = mode_list.get_ptr(); *mode_ptr != 0xffff; ++mode_ptr)
            {
                reg = { };
                reg.ax = 0x4f01;
                reg.cx = *mode_ptr;
                reg.es = mode_info.get_dos_ptr().segment;
                reg.di = mode_info.get_dos_ptr().offset;
                reg.call_rm_interrupt(0x10);
                // TODO: check ax for return value
                modes.push_back(*mode_info.get_ptr());
            }

            return info;
        }
    }
}
