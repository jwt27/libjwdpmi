/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <memory>
#include <jw/video/vbe.h>
#include <jw/math.h>

namespace jw
{
    namespace video
    {
        std::vector<byte> vbe2_pm_interface { };

        std::unique_ptr<dpmi::mapped_dos_memory<byte>> a000, b000, b800;
        std::unique_ptr<dpmi::memory<byte>> vbe3_stack { };
        std::vector<byte> video_bios { };
        std::vector<byte> bios_data_area { };
        detail::vbe3_pm_info* pmid { nullptr };

        std::vector<byte> vbe3_call_wrapper { };
        dpmi::far_ptr32 vbe3_call asm("vbe3_call") { };

        dpmi::linear_memory video_bios_data;
        dpmi::linear_memory video_bios_code;
        dpmi::linear_memory bios_data_area_mem;
        dpmi::linear_memory vbe3_call_wrapper_mem;

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
            using namespace dpmi;
            vbe2::init();

            {
                mapped_dos_memory<byte> video_bios_ptr { 64_KB, far_ptr16 { 0xC000, 0 } };
                auto* ptr = video_bios_ptr.get_ptr();
                video_bios.assign(ptr, ptr + 64_KB);
            }
            char* search_ptr = reinterpret_cast<char*>(video_bios.data());
            const char* search_value = "PMID";
            search_ptr = std::search(search_ptr, search_ptr + 64_KB, search_value, search_value + 4);
            if (strncmp(search_ptr, search_value, 4) != 0) return;
            pmid = reinterpret_cast<detail::vbe3_pm_info*>(search_ptr);
            if (checksum8(*pmid) != 0) return;

            bios_data_area.assign(1_KB, 0);
            bios_data_area_mem = { get_ds(), bios_data_area.data(), bios_data_area.size() };
            pmid->bda_selector = bios_data_area_mem.get_selector();
            auto ar = ldt_access_rights { get_ds() };
            ar.is_32_bit = false;
            bios_data_area_mem.get_ldt_entry().lock()->set_access_rights(ar);

            a000 = std::make_unique<mapped_dos_memory<byte>>(64_KB, far_ptr16 { 0xA000, 0 });
            b000 = std::make_unique<mapped_dos_memory<byte>>(64_KB, far_ptr16 { 0xB000, 0 });
            b800 = std::make_unique<mapped_dos_memory<byte>>(32_KB, far_ptr16 { 0xB800, 0 });
            pmid->a000_selector = a000->get_selector();
            pmid->b000_selector = b000->get_selector();
            pmid->b800_selector = b800->get_selector();

            video_bios_data = { get_ds(), video_bios.data(), video_bios.size() };
            video_bios_code = { get_ds(), video_bios.data(), video_bios.size() };
            video_bios_data.get_ldt_entry().lock()->set_access_rights(ar);
            ar.type = code_segment;
            video_bios_code.get_ldt_entry().lock()->set_access_rights(ar);
            pmid->code_selector = video_bios_data.get_selector();

            vbe3_stack = std::make_unique<memory<byte>>(4_KB);
            ar = ldt_access_rights { get_ss() };
            ar.is_32_bit = false;
            vbe3_stack->get_ldt_entry().lock()->set_access_rights(ar);
            auto stack_ptr = far_ptr32 { vbe3_stack->get_selector(), vbe3_stack->get_size() & -0x10 };
            auto entry_point = far_ptr32 { video_bios_code.get_selector(), pmid->init_entry_point };

            std::copy_n(reinterpret_cast<byte*>(&entry_point), 6, std::back_inserter(vbe3_call_wrapper));
            std::copy_n(reinterpret_cast<byte*>(&stack_ptr), 6, std::back_inserter(vbe3_call_wrapper));
            vbe3_call.offset = vbe3_call_wrapper.size();

            pmid->in_protected_mode = true;

            byte* code_start;
            std::size_t code_size;
            asm("jmp copy_end%=;"
                "copy_begin%=:"
                "push ebp;"
                "mov ebp, esp;"
                "mov bx, ss;"
                "lss esp, fword ptr cs:[6];"
                "call fword ptr cs:[0];"
                "mov ss, bx;"
                "mov esp, ebp;"
                "pop ebp;"
                "retf;"
                "copy_end%=:"
                "mov %0, offset copy_begin%=;"
                "mov %1, offset copy_end%=;"
                "sub %1, %0;"
                : "=rm,r" (code_start)
                , "=r,rm" (code_size)
                ::"cc");
            std::copy_n(code_start, code_size, std::back_inserter(vbe3_call_wrapper));
            vbe3_call_wrapper_mem = { get_ds(), vbe3_call_wrapper.data(), vbe3_call_wrapper.size() };
            ar = ldt_access_rights { get_cs() };
            vbe3_call_wrapper_mem.get_ldt_entry().lock()->set_access_rights(ar);

            asm volatile("call fword ptr [vbe3_call];");

            entry_point = far_ptr32 { video_bios_code.get_selector(), pmid->entry_point };
            std::copy_n(reinterpret_cast<byte*>(&entry_point), 6, vbe3_call_wrapper.data());
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
