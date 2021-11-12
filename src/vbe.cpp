/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#include <memory>
#include <optional>
#include <cstring>
#include <jw/video/vbe.h>
#include <jw/dpmi/memory.h>
#include <jw/dpmi/realmode.h>
#include <jw/math.h>

namespace jw
{
    namespace video
    {
        static std::vector<byte> vbe2_pm_interface { };
        static std::optional<dpmi::device_memory<byte>> vbe2_mmio;
        static std::uintptr_t vbe2_call_set_window;
        static std::uintptr_t vbe2_call_set_display_start;
        static std::uintptr_t vbe2_call_set_palette;
        static bool vbe2_pm { false };

        static std::optional<dpmi::mapped_dos_memory<byte>> a000, b000, b800;
        static std::optional<dpmi::memory<byte>> vbe3_stack;
        static std::optional<dpmi::memory<byte>> video_bios;
        static std::optional<dpmi::memory<byte>> bios_data_area;
        static detail::vbe3_pm_info* pmid { nullptr };

        static dpmi::linear_memory video_bios_code;
        static dpmi::far_ptr32 vbe3_stack_ptr;
        static dpmi::far_ptr32 vbe3_entry_point;
        static bool vbe3_pm { false };

        [[using gnu: naked, used, section(".text.low"), error("call only from asm")]]
        static void vbe3_call_wrapper() asm("vbe3");

        static void vbe3_call_wrapper()
        {
            asm
            (R"(
                push es
                push fs
                push gs
                push ebp
                mov ebp, esp
                mov si, ss
                lss esp, fword ptr cs:[%0]
                push si
                call fword ptr cs:[%1]
                pop si
                mov ss, si
                mov esp, ebp
                pop ebp
                pop gs
                pop fs
                pop es
                ret
            )" : : "i" (&vbe3_stack_ptr), "i" (&vbe3_entry_point)
            );
        }

        static void check_error(split_uint16_t ax, const char* function_name)
        {
            if (ax == 0x004f) [[likely]] return;
            std::stringstream msg { };
            msg << function_name << ": ";

            if (ax.lo != 0x4f)
            {
                msg << "VBE function not supported.";
                throw vbe::not_supported { msg.str() };
            }
            if (ax.hi == 0x01)
            {
                msg << "VBE function call failed.";
                throw vbe::failed { msg.str() };
            }
            if (ax.hi == 0x02)
            {
                msg << "VBE function call not supported in current hardware configuration.";
                throw vbe::not_supported_in_current_hardware { msg.str() };
            }
            if (ax.hi == 0x03)
            {
                msg << "VBE function call invalid in current video mode.";
                throw vbe::invalid_in_current_video_mode { msg.str() };
            }
            msg << "Unknown failure.";
            throw vbe::error { msg.str() };
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
            if (dpmi::descriptor::get_limit(dpmi::get_cs()) < cs_limit)
                dpmi::descriptor::set_limit(dpmi::get_cs(), cs_limit);

            {
                auto* ptr = reinterpret_cast<std::uint16_t*>(vbe2_pm_interface.data());
                vbe2_call_set_window = reinterpret_cast<std::uintptr_t>(ptr) + *(ptr + 0);
                vbe2_call_set_display_start = reinterpret_cast<std::uintptr_t>(ptr) + *(ptr + 1);
                vbe2_call_set_palette = reinterpret_cast<std::uintptr_t>(ptr) + *(ptr + 2);

                auto* io_list = reinterpret_cast<std::uint16_t*>(reinterpret_cast<std::uintptr_t>(ptr) + *(ptr + 3));
                if (*io_list != 0)
                {
                    while (*io_list != 0xffff) ++io_list;
                    if (*++io_list != 0xffff)
                    {
                        auto* addr = reinterpret_cast<std::uintptr_t*>(io_list);
                        auto* size = io_list + 2;
                        vbe2_mmio.emplace(*size, *addr);
                        auto ar = dpmi::ldt_access_rights { dpmi::get_ds() };
                        vbe2_mmio->get_descriptor().lock()->set_access_rights(ar);
                    }
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
                std::size_t bios_size;
                {
                    mapped_dos_memory<byte> video_bios_ptr { 128_KB, far_ptr16 { 0xC000, 0 } };
                    byte* ptr = video_bios_ptr.get_ptr();
                    bios_size = *(ptr + 2) * 512;
                    video_bios.emplace(bios_size);
                    std::copy_n(ptr, bios_size, video_bios->get_ptr());
                }
                char* search_ptr = reinterpret_cast<char*>(video_bios->get_ptr());
                const char* search_value = "PMID";
                search_ptr = std::search(search_ptr, search_ptr + bios_size, search_value, search_value + 4);
                if (std::strncmp(search_ptr, search_value, 4) != 0) return;
                pmid = reinterpret_cast<detail::vbe3_pm_info*>(search_ptr);
                if (checksum8(*pmid) != 0) return;
                pmid->in_protected_mode = true;

                bios_data_area.emplace(4_KB);
                std::fill_n(bios_data_area->get_ptr(), 4_KB, 0);
                pmid->bda_selector = bios_data_area->get_selector();
                ldt_access_rights ar { get_ds() };
                ar.is_32_bit = false;
                bios_data_area->get_descriptor().lock()->set_access_rights(ar);

                a000.emplace(64_KB, far_ptr16 { 0xA000, 0 });
                b000.emplace(64_KB, far_ptr16 { 0xB000, 0 });
                b800.emplace(32_KB, far_ptr16 { 0xB800, 0 });
                pmid->a000_selector = a000->get_selector();
                pmid->b000_selector = b000->get_selector();
                pmid->b800_selector = b800->get_selector();

                video_bios_code = { video_bios->get_address(), bios_size };
                video_bios->get_descriptor().lock()->set_access_rights(ar);
                ar = ldt_access_rights { get_cs() };
                ar.is_32_bit = false;
                video_bios_code.get_descriptor().lock()->set_access_rights(ar);
                pmid->data_selector = video_bios->get_selector();

                vbe3_stack.emplace(4_KB);
                ar = ldt_access_rights { get_ss() };
                ar.is_32_bit = false;
                vbe3_stack->get_descriptor().lock()->set_access_rights(ar);
                vbe3_stack_ptr = { vbe3_stack->get_selector(), (vbe3_stack->get_size() - 0x10) & -0x10 };
                vbe3_entry_point = { video_bios_code.get_selector(), pmid->init_entry_point };

                asm volatile("call vbe3" ::: "eax", "ebx", "ecx", "edx", "esi", "edi", "cc");

                vbe3_entry_point.offset = pmid->entry_point;
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
            dac_bits = 6;
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
            dac_bits = 6;
        }

        std::tuple<std::size_t, std::size_t, std::size_t> vbe::set_scanline_length(std::size_t width, bool width_in_pixels)
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

        std::tuple<std::size_t, std::size_t, std::size_t> vbe3::set_scanline_length(std::size_t width, bool width_in_pixels)
        {
            if (!vbe3_pm) return vbe2::set_scanline_length(width, width_in_pixels);

            std::uint16_t ax, pixels_per_scanline, bytes_per_scanline, max_scanlines;
            asm volatile(
                "call vbe3"
                : "=a" (ax)
                , "=b" (bytes_per_scanline)
                , "=c" (pixels_per_scanline)
                , "=d" (max_scanlines)
                : "a" (0x4f06)
                , "b" (width_in_pixels ? 0 : 2)
                , "c" (width)
                : "edi", "esi", "cc");
            check_error(ax, __PRETTY_FUNCTION__);
            return { pixels_per_scanline, bytes_per_scanline, max_scanlines };
        }

        std::tuple<std::size_t, std::size_t, std::size_t> vbe::get_scanline_length()
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

        std::tuple<std::size_t, std::size_t, std::size_t> vbe::get_max_scanline_length()
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

        std::tuple<std::size_t, std::size_t, std::size_t> vbe3::get_max_scanline_length()
        {
            if (!vbe3_pm) return vbe2::get_max_scanline_length();

            std::uint16_t ax, pixels_per_scanline, bytes_per_scanline, max_scanlines;
            asm("call vbe3"
                : "=a" (ax)
                , "=b" (bytes_per_scanline)
                , "=c" (pixels_per_scanline)
                , "=d" (max_scanlines)
                : "a" (0x4f06)
                , "b" (3)
                : "edi", "esi", "cc");
            check_error(ax, __PRETTY_FUNCTION__);
            return { pixels_per_scanline, bytes_per_scanline, max_scanlines };
        }

        void vbe::set_display_start(vector2i pos, bool wait_for_vsync)
        {
            dpmi::realmode_registers reg { };
            reg.ax = 0x4f07;
            reg.bx = wait_for_vsync ? 0x80 : 0;
            reg.cx = pos.x();
            reg.dx = pos.y();
            reg.call_int(0x10);
            check_error(reg.ax, __PRETTY_FUNCTION__);
        }

        void vbe2::set_display_start(vector2i pos, bool wait_for_vsync)
        {
            if (!vbe2_pm) return vbe::set_display_start(pos, wait_for_vsync);

            auto bps = (mode.use_lfb_mode && info.vbe_version >= 0x300) ? mode_info->linear_bytes_per_scanline : mode_info->bytes_per_scanline;
            auto start = pos.x() * (mode_info->bits_per_pixel / 8) + pos.y() * bps;
            if (mode_info->bits_per_pixel >= 8) start = ((start & 3) << 30 | (start >> 2));
            split_uint32_t split_start { start };

            dpmi::selector mmio = vbe2_mmio ? vbe2_mmio->get_selector() : dpmi::get_ds();
            std::uint16_t ax;
            force_frame_pointer();
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
                , "c" (split_start.lo)
                , "d" (split_start.hi)
                : "edi", "esi", "cc");
        }

        void vbe3::set_display_start(vector2i pos, bool wait_for_vsync)
        {
            if (!vbe3_pm) return vbe2::set_display_start(pos, wait_for_vsync);

            std::uint16_t ax;
            asm volatile(
                "call vbe3"
                : "=a" (ax)
                : "a" (0x4f07)
                , "b" (wait_for_vsync ? 0x80 : 0)
                , "c" (pos.x())
                , "d" (pos.y())
                : "edi", "esi", "cc");
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

        void vbe::schedule_display_start(vector2i pos)
        {
            return set_display_start(pos, false);
        }

        void vbe3::schedule_display_start(vector2i pos)
        {
            if (not mode_info->attr.triple_buffering_supported) return set_display_start(pos);

            auto bps = mode.use_lfb_mode ? mode_info->linear_bytes_per_scanline : mode_info->bytes_per_scanline;
            auto start = pos.x() * (mode_info->bits_per_pixel / 8) + pos.y() * bps;

            if (vbe3_pm)
            {
                std::uint16_t ax;
                asm volatile(
                    "call vbe3"
                    : "=a" (ax)
                    : "a" (0x4f07)
                    , "b" (2)
                    , "c" (start)
                    : "edx", "edi", "esi", "cc");
                check_error(ax, __PRETTY_FUNCTION__);
            }
            else
            {
                dpmi::realmode_registers reg { };
                reg.ax = 0x4f07;
                reg.bx = 2;
                reg.ecx = start;
                reg.call_int(0x10);
                check_error(reg.ax, __PRETTY_FUNCTION__);
            }
        }

        bool vbe::get_scheduled_display_start_status()
        {
            return true;
        }

        bool vbe3::get_scheduled_display_start_status()
        {
            if (!mode_info->attr.triple_buffering_supported) return vbe2::get_scheduled_display_start_status();

            if (vbe3_pm)
            {
                std::uint16_t ax, cx;
                asm("call vbe3"
                    : "=a" (ax)
                    , "=c" (cx)
                    : "a" (0x4f07)
                    , "b" (4)
                    : "edx", "edi", "esi", "cc");
                check_error(ax, __PRETTY_FUNCTION__);
                return cx != 0;
            }
            else
            {
                dpmi::realmode_registers reg { };
                reg.ax = 0x4f07;
                reg.bx = 4;
                reg.call_int(0x10);
                check_error(reg.ax, __PRETTY_FUNCTION__);
                return reg.cx != 0;
            }
        }

        std::uint8_t vbe::set_palette_format(std::uint8_t bits_per_channel)
        {
            dpmi::realmode_registers reg { };
            reg.ax = 0x4f08;
            reg.bh = bits_per_channel;
            reg.call_int(0x10);
            check_error(reg.ax, __PRETTY_FUNCTION__);
            dac_bits = reg.bh;
            return reg.bh;
        }

        std::uint8_t vbe3::set_palette_format(std::uint8_t bits_per_channel)
        {
            if (!vbe3_pm) return vbe2::set_palette_format(bits_per_channel);

            std::uint16_t ax;
            split_uint16_t bx;
            asm volatile(
                "call vbe3"
                : "=a" (ax)
                , "=b" (bx)
                : "a" (0x4f08)
                , "b" (bits_per_channel << 8)
                : "ecx", "edx", "edi", "esi", "cc");
            check_error(ax, __PRETTY_FUNCTION__);
            dac_bits = bx.hi;
            return bx.hi;
        }

        std::uint8_t vbe::get_palette_format()
        {
            dpmi::realmode_registers reg { };
            reg.ax = 0x4f08;
            reg.bx = 1;
            reg.call_int(0x10);
            check_error(reg.ax, __PRETTY_FUNCTION__);
            dac_bits = reg.bh;
            return reg.bh;
        }

        void vbe2::set_palette(const px32n* begin, const px32n* end, std::size_t first, bool wait_for_vsync)
        {
            //return vga::set_palette(begin, end, first, wait_for_vsync);
            auto size = std::min(static_cast<std::size_t>(end - begin), std::size_t { 256 });
            if (vbe2_pm)
            {
                std::unique_ptr<std::vector<pxvga>> copy;
                const byte* ptr = reinterpret_cast<const byte*>(begin);
                if (dac_bits < 8)
                {
                    copy = std::make_unique<std::vector<pxvga>>(begin, end);
                    ptr = reinterpret_cast<const byte*>(copy->data());
                }

                dpmi::selector mmio = vbe2_mmio ? vbe2_mmio->get_selector() : dpmi::get_ds();
                force_frame_pointer();
                asm volatile(
                    "push ds;"
                    "push es;"
                    "push ds;"
                    "pop es;"
                    "mov ds, es:%w1;"
                    "call es:%0;"
                    "pop es;"
                    "pop ds;"
                    :: "m" (vbe2_call_set_palette)
                    , "m" (mmio)
                    , "a" (0x4f09)
                    , "b" (wait_for_vsync ? 0x80 : 0)
                    , "c" (size)
                    , "d" (first)
                    , "D" (ptr)
                    : "esi", "cc");
            }
            else
            {
                dpmi::dos_memory<px32n> dos_data { size };
                if (dac_bits < 8)
                {
                    for (std::size_t i = 0; i < size; ++i)
                        new(reinterpret_cast<pxvga*>(dos_data.get_ptr() + i)) pxvga { begin[i] };
                }
                else
                {
                    for (std::size_t i = 0; i < size; ++i)
                        new(dos_data.get_ptr() + i) px32n { std::move(begin[i]) };
                }

                dpmi::realmode_registers reg { };
                reg.ax = 0x4f09;
                reg.bx = wait_for_vsync ? 0x80 : 0;
                reg.cx = size;
                reg.dx = first;
                reg.es = dos_data.get_dos_ptr().segment;
                reg.di = dos_data.get_dos_ptr().offset;
                reg.call_int(0x10);
                check_error(reg.ax, __PRETTY_FUNCTION__);
            }
        }

        void vbe3::set_palette(const px32n* begin, const px32n* end, std::size_t first, bool wait_for_vsync)
        {
            if (not vbe3_pm) return vbe2::set_palette(begin, end, first, wait_for_vsync);

            std::unique_ptr<std::vector<pxvga>> copy;
            const byte* ptr = reinterpret_cast<const byte*>(begin);
            if (dac_bits < 8)
            {
                copy = std::make_unique<std::vector<pxvga>>(begin, end);
                ptr = reinterpret_cast<const byte*>(copy->data());
            }

            dpmi::linear_memory data_mem { dpmi::get_ds(), reinterpret_cast<const px32n*>(ptr),  static_cast<std::size_t>(end - begin) };
            std::uint16_t ax;
            force_frame_pointer();
            asm volatile(
                "push es;"
                "mov es, %w1;"
                "call vbe3;"
                "pop es;"
                : "=a" (ax)
                : "rm" (data_mem.get_selector())
                , "a" (0x4f09)
                , "b" (wait_for_vsync ? 0x80 : 0)
                , "c" (std::min(end - begin, std::ptrdiff_t { 256 }))
                , "d" (first)
                , "D" (0)
                : "esi", "cc");
            check_error(ax, __PRETTY_FUNCTION__);
        }

        std::vector<px32n> vbe2::get_palette()
        {
            dpmi::dos_memory<px32n> dos_data { 256 };

            dpmi::realmode_registers reg { };
            reg.ax = 0x4f09;
            reg.bx = 1;
            reg.cx = 256;
            reg.dx = 0;
            reg.es = dos_data.get_dos_ptr().segment;
            reg.di = dos_data.get_dos_ptr().offset;
            reg.call_int(0x10);
            if (info.vbe_version < 0x300) check_error(reg.ax, __PRETTY_FUNCTION__);
            else try { check_error(reg.ax, __PRETTY_FUNCTION__); }
            catch (const error&) { return vga::get_palette(); }

            std::vector<px32n> result;
            result.reserve(256);
            auto* ptr = dos_data.get_ptr();
            if (dac_bits < 8) for (auto i = 0; i < 256; ++i) result.push_back(*(reinterpret_cast<pxvga*>(ptr) + i));
            else for (auto i = 0; i < 256; ++i) result.push_back(ptr[i]);
            return result;
        }

        std::uint32_t vbe3::get_closest_pixel_clock(std::uint32_t desired_clock, std::uint16_t mode_num)
        {
            if (vbe3_pm)
            {
                std::uint16_t ax;
                std::uint32_t ecx;
                asm("call vbe3"
                    : "=a" (ax)
                    , "=c" (ecx)
                    : "a" (0x4f0b)
                    , "b" (0)
                    , "c" (desired_clock)
                    , "d" (mode_num)
                    : "edi", "esi", "cc");
                check_error(ax, __PRETTY_FUNCTION__);
                return ecx;
            }
            else
            {
                dpmi::realmode_registers reg { };
                reg.ax = 0x4f0b;
                reg.bl = 0;
                reg.ecx = desired_clock;
                reg.dx = mode_num;
                reg.call_int(0x10);
                check_error(reg.ax, __PRETTY_FUNCTION__);
                return reg.cx != 0;
            }
        }
    }
}
