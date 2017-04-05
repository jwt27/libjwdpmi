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
        std::unique_ptr<dpmi::device_memory<byte>> vbe2_mmio;
        std::uintptr_t vbe2_call_set_window;
        std::uintptr_t vbe2_call_set_display_start;
        std::uintptr_t vbe2_call_set_palette;
        bool vbe2_pm { false };

        std::unique_ptr<dpmi::mapped_dos_memory<byte>> a000, b000, b800;
        std::unique_ptr<dpmi::memory<byte>> vbe3_stack { };
        std::unique_ptr<dpmi::memory<byte>> video_bios { };
        std::unique_ptr<dpmi::memory<byte>> bios_data_area { };
        detail::vbe3_pm_info* pmid { nullptr };

        std::vector<byte> vbe3_call_wrapper { };
        dpmi::far_ptr32 vbe3_call asm("vbe3_call") { };

        dpmi::linear_memory video_bios_code;
        dpmi::linear_memory vbe3_call_wrapper_mem;
        bool vbe3_pm { false };

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
            auto get_mode = [&](std::uint16_t num)
            {
                *mode_info = { };
                dpmi::realmode_registers reg { };
                reg.ax = 0x4f01;
                reg.cx = num;
                reg.es = mode_info.get_dos_ptr().segment;
                reg.di = mode_info.get_dos_ptr().offset;
                reg.call_int(0x10);
                check_error(reg.ax, __PRETTY_FUNCTION__);
                if (mode_info->attr.is_supported) modes[num] = *mode_info;
            };

            for (auto* mode_ptr = mode_list.get_ptr(); *mode_ptr != 0xffff; ++mode_ptr)
                get_mode(*mode_ptr);

            for (auto n = 0; n < 0x7f; ++n)
            {
                try { get_mode(n); }
                catch (const error&) { }
            }
            try { get_mode(0x81ff); }
            catch (const error&) { }
        }

        const vbe_info& vbe::get_vbe_info()
        {
            if (info.vbe_signature != "VESA") init();
            return info;
        }

        void vbe::init()
        {
            if (info.vbe_signature == "VESA") return;
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
            if (info.vbe_signature == "VESA") return;
            dpmi::dos_memory<detail::raw_vbe_info> raw_info { 1 };
            auto* ptr = raw_info.get_ptr();
            std::copy_n("VBE2", 4, ptr->vbe_signature);

            dpmi::realmode_registers reg { };
            reg.ax = 0x4f00;
            reg.es = raw_info.get_dos_ptr().segment;
            reg.di = raw_info.get_dos_ptr().offset;
            reg.call_int(0x10);
            check_error(reg.ax, __PRETTY_FUNCTION__);
            if (ptr->vbe_version < 0x0200) throw not_supported { "VBE2+ not supported." };

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

            if (vbe2_pm) return;
            reg = { };
            reg.ax = 0x4f0a;
            reg.bl = 0;
            reg.call_int(0x10);
            check_error(reg.ax, __PRETTY_FUNCTION__);
            dpmi::mapped_dos_memory<byte> pm_table { reg.cx, dpmi::far_ptr16 { reg.es, reg.di } };
            byte* pm_table_ptr = pm_table.get_ptr();
            vbe2_pm_interface.assign(pm_table_ptr, pm_table_ptr + reg.cx);
            auto cs_limit = reinterpret_cast<std::size_t>(vbe2_pm_interface.data() + reg.cx);
            if (dpmi::ldt_entry::get_limit(dpmi::get_cs()) < cs_limit) 
                dpmi::ldt_entry::set_limit(dpmi::get_cs(), cs_limit);

            {
                auto* ptr = vbe2_pm_interface.data();
                vbe2_call_set_window = reinterpret_cast<std::uintptr_t>(ptr) + *reinterpret_cast<std::uint16_t*>(ptr + 0);
                vbe2_call_set_display_start = reinterpret_cast<std::uintptr_t>(ptr) + *reinterpret_cast<std::uint16_t*>(ptr + 2);
                vbe2_call_set_palette = reinterpret_cast<std::uintptr_t>(ptr) + *reinterpret_cast<std::uint16_t*>(ptr + 4);
            }

            auto* io_list = reinterpret_cast<std::uint16_t*>(vbe2_pm_interface.data());
            if (*io_list != 0)
            {
                while (*io_list != 0xffff) ++io_list;
                if (*++io_list != 0xffff)
                {
                    auto* addr = reinterpret_cast<std::uintptr_t*>(io_list);
                    auto* size = io_list + 2;
                    vbe2_mmio = std::make_unique<dpmi::device_memory<byte>>(*size, *addr);
                    auto ar = dpmi::ldt_access_rights { dpmi::get_ds() };
                    vbe2_mmio->get_ldt_entry().lock()->set_access_rights(ar);
                }
            }
            vbe2_pm = true;
        }

        void vbe3::init()
        {
            using namespace dpmi;
            vbe2::init();
            if (info.vbe_version < 0x0300) throw not_supported { "VBE3+ not supported." };
            if (vbe3_pm) return;

            try
            {
                {
                    mapped_dos_memory<byte> video_bios_ptr { 64_KB, far_ptr16 { 0xC000, 0 } };
                    auto* ptr = video_bios_ptr.get_ptr();
                    video_bios = std::make_unique<memory<byte>>(64_KB);
                    std::copy_n(ptr, 64_KB, video_bios->get_ptr());
                }
                char* search_ptr = reinterpret_cast<char*>(video_bios->get_ptr());
                const char* search_value = "PMID";
                search_ptr = std::search(search_ptr, search_ptr + 64_KB, search_value, search_value + 4);
                if (std::strncmp(search_ptr, search_value, 4) != 0) return;
                pmid = reinterpret_cast<detail::vbe3_pm_info*>(search_ptr);
                if (checksum8(*pmid) != 0) return;
                pmid->in_protected_mode = true;

                bios_data_area = std::make_unique<memory<byte>>(4_KB);
                std::fill_n(bios_data_area->get_ptr(), 4_KB, 0);
                pmid->bda_selector = bios_data_area->get_selector();
                ldt_access_rights ar { get_ds() };
                ar.is_32_bit = false;
                bios_data_area->get_ldt_entry().lock()->set_access_rights(ar);

                a000 = std::make_unique<mapped_dos_memory<byte>>(64_KB, far_ptr16 { 0xA000, 0 });
                b000 = std::make_unique<mapped_dos_memory<byte>>(64_KB, far_ptr16 { 0xB000, 0 });
                b800 = std::make_unique<mapped_dos_memory<byte>>(32_KB, far_ptr16 { 0xB800, 0 });
                pmid->a000_selector = a000->get_selector();
                pmid->b000_selector = b000->get_selector();
                pmid->b800_selector = b800->get_selector();

                video_bios_code = { video_bios->get_address(), 64_KB };
                video_bios->get_ldt_entry().lock()->set_access_rights(ar);
                ar = ldt_access_rights { get_cs() };
                ar.is_32_bit = false;
                video_bios_code.get_ldt_entry().lock()->set_access_rights(ar);
                pmid->data_selector = video_bios->get_selector();

                vbe3_stack = std::make_unique<memory<byte>>(4_KB);
                ar = ldt_access_rights { get_ss() };
                ar.is_32_bit = false;
                vbe3_stack->get_ldt_entry().lock()->set_access_rights(ar);
                far_ptr32 stack_ptr { vbe3_stack->get_selector(), (vbe3_stack->get_size() - 0x10) & -0x10 };
                far_ptr16 entry_point { video_bios_code.get_selector(), pmid->init_entry_point };

                std::copy_n(reinterpret_cast<byte*>(&entry_point), sizeof(far_ptr16), std::back_inserter(vbe3_call_wrapper));
                std::copy_n(reinterpret_cast<byte*>(&stack_ptr), sizeof(far_ptr32), std::back_inserter(vbe3_call_wrapper));
                vbe3_call.offset = vbe3_call_wrapper.size();

                byte* code_start;
                std::size_t code_size;
                asm("jmp copy_end%=;"
                    "copy_begin%=:"
                    "push es; push fs; push gs;"
                    "push ebp;"
                    "mov ebp, esp;"
                    "mov si, ss;"
                    "lss esp, fword ptr cs:[4];"
                    "push si;"
                    ".byte 0x66;"   // use "short" fword ptr
                    "call fword ptr cs:[0];"
                    "pop si;"
                    "mov ss, si;"
                    "mov esp, ebp;"
                    "pop ebp;"
                    "pop gs; pop fs; pop es;"
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
                vbe3_call.segment = vbe3_call_wrapper_mem.get_selector();
                auto cs_limit = reinterpret_cast<std::size_t>(vbe3_call_wrapper.data() + code_size);
                if (ldt_entry::get_limit(get_cs()) < cs_limit) 
                    ldt_entry::set_limit(get_cs(), cs_limit);

                asm volatile("call fword ptr [vbe3_call];":::"eax", "ebx", "ecx", "edx", "esi", "edi", "cc");

                entry_point.offset = pmid->entry_point;
                std::copy_n(reinterpret_cast<byte*>(&entry_point), sizeof(far_ptr16), vbe3_call_wrapper.data());
                vbe3_pm = true;
            }
            catch (...)
            {
                a000.reset();
                b000.reset();
                b800.reset();
                vbe3_stack.reset();
                video_bios.reset();
                bios_data_area.reset();
                throw;
            }
        }

        void vbe::set_mode(vbe_mode m, const crtc_info*)
        {
            dpmi::realmode_registers reg { };
            reg.ax = 0x4f02;
            reg.bx = m.raw_value;
            reg.call_int(0x10);
            check_error(reg.ax, __PRETTY_FUNCTION__);
            mode = m;
            mode_info = &modes[m.mode];
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
            mode = m;
            mode_info = &modes[m.mode];
        } 

        std::tuple<std::uint32_t, std::uintptr_t, std::uint32_t> vbe::set_scanline_length(std::uint32_t width, bool width_in_pixels)
        {
            dpmi::realmode_registers reg { };
            reg.ax = 0x4f06;
            reg.bl = width_in_pixels ? 0 : 2;
            reg.cx = width;
            reg.call_int(0x10);
            check_error(reg.ax, __PRETTY_FUNCTION__);
            std::uint16_t pixels_per_scanline = reg.cx;
            std::uint16_t bytes_per_scanline = reg.bx;
            std::uint16_t max_scanlines = reg.dx;
            return { pixels_per_scanline, bytes_per_scanline, max_scanlines };
        }

        std::tuple<std::uint32_t, std::uintptr_t, std::uint32_t> vbe3::set_scanline_length(std::uint32_t width, bool width_in_pixels)
        {
            if (!vbe3_pm) return vbe2::set_scanline_length(width, width_in_pixels);

            std::uint16_t ax, pixels_per_scanline, bytes_per_scanline, max_scanlines;
            asm volatile(
                "call fword ptr [vbe3_call];"
                : "=a" (ax)
                , "=b" (bytes_per_scanline)
                , "=c" (pixels_per_scanline)
                , "=d" (max_scanlines)
                : "a" (0x4f06)
                , "b" (width_in_pixels ? 0 : 2)
                , "c" (width)
                : "edi", "esi", "memory", "cc");
            check_error(ax, __PRETTY_FUNCTION__);
            return { pixels_per_scanline, bytes_per_scanline, max_scanlines };
        }

        std::tuple<std::uint32_t, std::uintptr_t, std::uint32_t> vbe::get_scanline_length()
        {
            dpmi::realmode_registers reg { };
            reg.ax = 0x4f06;
            reg.bl = 1;
            reg.call_int(0x10);
            check_error(reg.ax, __PRETTY_FUNCTION__);
            std::uint16_t pixels_per_scanline = reg.cx;
            std::uint16_t bytes_per_scanline = reg.bx;
            std::uint16_t max_scanlines = reg.dx;
            return { pixels_per_scanline, bytes_per_scanline, max_scanlines };
        }

        std::tuple<std::uint32_t, std::uintptr_t, std::uint32_t> vbe::get_max_scanline_length()
        {
            dpmi::realmode_registers reg { };
            reg.ax = 0x4f06;
            reg.bl = 3;
            reg.call_int(0x10);
            check_error(reg.ax, __PRETTY_FUNCTION__);
            std::uint16_t pixels_per_scanline = reg.cx;
            std::uint16_t bytes_per_scanline = reg.bx;
            std::uint16_t max_scanlines = reg.dx;
            return { pixels_per_scanline, bytes_per_scanline, max_scanlines };
        }

        std::tuple<std::uint32_t, std::uintptr_t, std::uint32_t> vbe3::get_max_scanline_length()
        {
            if (!vbe3_pm) return vbe2::get_max_scanline_length();

            std::uint16_t ax, pixels_per_scanline, bytes_per_scanline, max_scanlines;
            asm("call fword ptr [vbe3_call];"
                : "=a" (ax)
                , "=b" (bytes_per_scanline)
                , "=c" (pixels_per_scanline)
                , "=d" (max_scanlines)
                : "a" (0x4f06)
                , "b" (3)
                : "edi", "esi", "memory", "cc");
            check_error(ax, __PRETTY_FUNCTION__);
            return { pixels_per_scanline, bytes_per_scanline, max_scanlines };
        }

        void vbe::set_display_start(vector2i pos, bool wait_for_vsync)
        {
            dpmi::realmode_registers reg { };
            reg.ax = 0x4f07;
            reg.bx = wait_for_vsync ? 0x80 : 0;
            reg.cx = pos.x;
            reg.dx = pos.y;
            reg.call_int(0x10);
            check_error(reg.ax, __PRETTY_FUNCTION__);
        }

        void vbe2::set_display_start(vector2i pos, bool wait_for_vsync)
        {
            //if (!vbe2_pm)
            return vbe::set_display_start(pos, wait_for_vsync);

            dpmi::selector mmio = vbe2_mmio ? vbe2_mmio->get_selector() : dpmi::get_ds();
            std::uint16_t ax;
            asm volatile(
                "push es;"
                "mov es, %w2;"
                "call %1;"
                "pop es;"
                : "=a" (ax)
                : "rm" (vbe2_call_set_display_start)
                , "rm" (mmio)
                , "a" (0x4f07)
                , "b" (wait_for_vsync ? 0x80 : 0)
                , "c" (0)   // TODO: calculate from current mode dimensions
                , "d" (0)
                : "edi", "esi", "memory", "cc");
            check_error(ax, __PRETTY_FUNCTION__);
        }

        void vbe3::set_display_start(vector2i pos, bool wait_for_vsync)
        {
            if (!vbe3_pm) return vbe2::set_display_start(pos, wait_for_vsync);

            std::uint16_t ax;
            asm volatile(
                "call fword ptr [vbe3_call];"
                : "=a" (ax)
                : "a" (0x4f07)
                , "b" (wait_for_vsync ? 0x80 : 0)
                , "c" (pos.x)
                , "d" (pos.y)
                : "edi", "esi", "memory", "cc");
            check_error(ax, __PRETTY_FUNCTION__);
        }

        vector2i vbe::get_display_start()
        {
            dpmi::realmode_registers reg { };
            reg.ax = 0x4f07;
            reg.bx = 1;
            reg.call_int(0x10);
            check_error(reg.ax, __PRETTY_FUNCTION__);
            std::uint16_t first_pixel = reg.cx;
            std::uint16_t first_scanline = reg.dx;
            return { first_pixel, first_scanline };
        }

        vector2i vbe3::get_display_start()
        {
            if (!vbe3_pm) return vbe2::get_display_start();

            std::uint16_t ax, first_pixel, first_scanline;
            asm volatile(
                "call fword ptr [vbe3_call];"
                : "=a" (ax)
                , "=c" (first_pixel)
                , "=d" (first_scanline)
                : "a" (0x4f07)
                , "b" (1)
                : "edi", "esi", "memory", "cc");
            check_error(ax, __PRETTY_FUNCTION__);
            return { first_pixel, first_scanline };
        }
    }
}
