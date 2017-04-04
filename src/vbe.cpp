/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <jw/video/vbe.h>

namespace jw
{
    namespace video
    {
        std::vector<byte> vbe2_pm_interface { };

        dpmi::far_ptr32 vbe3_call { };

        void vbe::check_error(split_uint16_t ax, const char* function_name)
        {
            if (ax == 0x004f) return;
            std::stringstream msg { };
            msg << function_name << ": ";

            if (ax.lo != 0x4f)
            {
                msg << "VBE function not supported.";
                throw not_supported { msg.str() };
            }
            if (ax.hi == 0x01)
            {
                msg << "VBE function call failed.";
                throw failed { msg.str() };
            }
            if (ax.hi == 0x02)
            {
                msg << "VBE function call not supported in current hardware configuration.";
                throw not_supported_in_current_hardware { msg.str() };
            }
            if (ax.hi == 0x03)
            {
                msg << "VBE function call invalid in current video mode.";
                throw invalid_in_current_video_mode { msg.str() };
            }
            msg << "Unknown failure.";
            throw error { msg.str() };
        }

        void vbe::populate_mode_list(dpmi::far_ptr16 list_ptr)
        {
            dpmi::mapped_dos_memory<std::uint16_t> mode_list { 256, list_ptr };
            dpmi::dos_memory<vbe_mode_info> mode_info { 1 };
            for (auto* mode_ptr = mode_list.get_ptr(); *mode_ptr != 0xffff; ++mode_ptr)
            {
                *mode_info = { };
                dpmi::realmode_registers reg { };
                reg.ax = 0x4f01;
                reg.cx = *mode_ptr;
                reg.es = mode_info.get_dos_ptr().segment;
                reg.di = mode_info.get_dos_ptr().offset;
                reg.call_int(0x10);
                check_error(reg.ax, __PRETTY_FUNCTION__);
                modes.push_back(*mode_info);
            }
        }

        const vbe_info& vbe::get_vbe_info()
        {
            if (info.vbe_signature != "VESA") init();
            return info;
        }

        void vbe::init()
        {
            dpmi::dos_memory<detail::raw_vbe_info> raw_info { 1 };
            auto* ptr = raw_info.get_ptr();

            dpmi::realmode_registers reg { };
            reg.ax = 0x4f00;
            reg.es = raw_info.get_dos_ptr().segment;
            reg.di = raw_info.get_dos_ptr().offset;
            reg.call_int(0x10);
            check_error(reg.ax, __PRETTY_FUNCTION__);

            info.vbe_signature.assign(ptr->vbe_signature, ptr->vbe_signature + 4);
            info.vbe_version = ptr->vbe_version;

            std::copy_n(&ptr->capabilities, 1, reinterpret_cast<std::uint32_t*>(&info.capabilities));
            info.total_memory = ptr->total_memory;
            {
                dpmi::mapped_dos_memory<char> str { 256, ptr->oem_string };
                info.oem_string = str.get_ptr();
            }
            populate_mode_list(ptr->video_mode_list);
        }

        void vbe2::init()
        {
            dpmi::dos_memory<detail::raw_vbe_info> raw_info { 1 };
            auto* ptr = raw_info.get_ptr();
            std::copy_n("VBE2", 4, ptr->vbe_signature);

            dpmi::realmode_registers reg { };
            reg.ax = 0x4f00;
            reg.es = raw_info.get_dos_ptr().segment;
            reg.di = raw_info.get_dos_ptr().offset;
            reg.call_int(0x10);
            check_error(reg.ax, __PRETTY_FUNCTION__);
            if (strncmp(ptr->vbe_signature, "VESA", 4) != 0) throw not_supported { "VBE2+ not supported." };

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
            populate_mode_list(ptr->video_mode_list);

            reg = { };
            reg.ax = 0x4f0a;
            reg.bl = 0;
            reg.call_int(0x10);
            check_error(reg.ax, __PRETTY_FUNCTION__);
            dpmi::mapped_dos_memory<byte> pm_table { reg.cx, dpmi::far_ptr16 { reg.es, reg.di } };
            byte* pm_table_ptr = pm_table.get_ptr();
            vbe2_pm_interface.assign(pm_table_ptr, pm_table_ptr + reg.cx);
        }

        void vbe3::init()
        {
            vbe2::init();
        }

        void vbe::set_mode(vbe_mode m, const crtc_info*)
        {
            dpmi::realmode_registers reg { };
            reg.ax = 0x4f02;
            reg.bx = m.raw_value;
            reg.call_int(0x10);
            check_error(reg.ax, __PRETTY_FUNCTION__);
        }

        void vbe3::set_mode(vbe_mode m, const crtc_info* crtc)
        {
            if (crtc == nullptr) m.use_custom_crtc_timings = false;
            dpmi::realmode_registers reg { };
            reg.ax = 0x4f02;
            reg.bx = m.raw_value;
            if (m.use_custom_crtc_timings)
            {
                dpmi::dos_memory<crtc_info> crtc_ptr { 1 };
                *crtc_ptr = *crtc;
                reg.es = crtc_ptr.get_dos_ptr().segment;
                reg.di = crtc_ptr.get_dos_ptr().offset;
                reg.call_int(0x10);
            }
            else reg.call_int(0x10);
            check_error(reg.ax, __PRETTY_FUNCTION__);
        }
    }
}
