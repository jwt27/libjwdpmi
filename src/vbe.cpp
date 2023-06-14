#/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
#/*    Copyright (C) 2017 - 2023 J.W. Jagersma, see COPYING.txt for details    */

#include <memory>
#include <optional>
#include <cstring>
#include <jw/video/vbe.h>
#include <jw/dpmi/memory.h>
#include <jw/dpmi/realmode.h>
#include <jw/math.h>

namespace jw::video
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpacked-not-aligned"
    struct [[gnu::packed]] raw_vbe_info
    {
        char vbe_signature[4];
        std::uint16_t vbe_version;
        dpmi::far_ptr16 oem_string;
        std::uint32_t capabilities;
        dpmi::far_ptr16 video_mode_list;
        std::uint16_t total_memory;
        std::uint16_t oem_software_ver;
        dpmi::far_ptr16 oem_vendor_name;
        dpmi::far_ptr16 oem_product_name;
        dpmi::far_ptr16 oem_product_version;
        byte _reserved[222];
        byte oem_data[256];
    };

    static_assert (sizeof(raw_vbe_info) == 0x200);
#pragma GCC diagnostic pop

    struct vbe3_pm_info
    {
        char pmid[4];
        std::uint16_t entry_point;
        std::uint16_t init_entry_point;
        dpmi::selector bda_selector;
        dpmi::selector a000_selector;
        dpmi::selector b000_selector;
        dpmi::selector b800_selector;
        dpmi::selector data_selector;
        bool in_protected_mode;
        byte checksum;
    };

    static_assert (sizeof(vbe3_pm_info) == 0x14);

    union dos_data_t
    {
        std::array<px32n, 256> palette;
        vbe_mode_info mode;
        crtc_info crtc;
        raw_vbe_info raw_vbe;
    };

    static auto& get_dos_data()
    {
        static dpmi::dos_memory<dos_data_t> data { 1 };
        return data;
    }

    static auto& get_realmode_registers()
    {
        static constinit dpmi::realmode_registers reg { };
        reg.ss = reg.sp = 0;
        reg.flags.interrupt = true;
        return reg;
    }

    static std::aligned_union_t<0, vbe, vbe2, vbe3> instance;
    static vbe* instance_ptr { nullptr };

    static vbe_info bios_info;
    static std::map<std::uint_fast16_t, vbe_mode_info> mode_list { };
    static vbe_mode mode;
    static vbe_mode_info* mode_info { nullptr };

    static std::vector<std::byte> vbe2_pm_interface { };
    static std::optional<dpmi::device_memory<byte>> vbe2_mmio_memory;
    static std::optional<dpmi::descriptor> vbe2_mmio;
    static std::uintptr_t vbe2_call_set_window;
    static std::uintptr_t vbe2_call_set_display_start;
    static std::uintptr_t vbe2_call_set_palette;
    static bool vbe2_pm { false };

    static std::unique_ptr<std::byte[]> vbe3_stack_memory;
    static std::unique_ptr<std::byte[]> video_bios_memory;
    static std::unique_ptr<std::byte[]> fake_bda_memory;
    static std::optional<dpmi::descriptor> vbe3_stack;
    static std::optional<dpmi::descriptor> video_bios;
    static std::optional<dpmi::descriptor> fake_bda;
    static vbe3_pm_info* pmid { nullptr };

    static std::optional<dpmi::descriptor> video_bios_code;
    static dpmi::far_ptr32 vbe3_stack_ptr;
    static dpmi::far_ptr16 vbe3_entry_point;
    static bool vbe3_pm { false };

    [[using gnu: naked, used, section(".text.low"), error("call only from asm")]]
    static void vbe3_call_wrapper() asm("vbe3");

    static void vbe3_call_wrapper()
    {
        asm
        (R"(
            push ebp
            mov ebp, esp
            mov esi, ss
            lss esp, fword ptr ds:[%0]
            data16 call fword ptr ds:[%1]
            mov ss, esi
            mov esp, ebp
            pop ebp
            ret
        )" : : "i" (&vbe3_stack_ptr), "i" (&vbe3_entry_point)
        );
    }

    static void check_error(split_uint16_t ax, const char* function_name)
    {
        if (ax == 0x004f) [[likely]] return;
        std::string msg { function_name };
        msg += ": VBE function ";

        if (ax.lo != 0x4f)
        {
            msg += "not supported.";
            throw vbe::not_supported { msg };
        }
        if (ax.hi == 0x01)
        {
            msg += "call failed.";
            throw vbe::failed { msg };
        }
        if (ax.hi == 0x02)
        {
            msg += "not supported in current hardware configuration.";
            throw vbe::not_supported_in_current_hardware { msg };
        }
        if (ax.hi == 0x03)
        {
            msg += "call invalid in current video mode.";
            throw vbe::invalid_in_current_video_mode { msg };
        }
        msg += "call - unknown failure.";
        throw vbe::error { msg };
    }

    vbe* vbe_interface()
    {
        if (instance_ptr) [[likely]] return instance_ptr;

        auto& dos_data = get_dos_data();
        auto* ptr = &dos_data->raw_vbe;

        // Request VBE 2+ information, if available.
        std::copy_n("VBE2", 4, ptr->vbe_signature);

        auto& reg = get_realmode_registers();
        reg.ax = 0x4f00;
        reg.es = dos_data.dos_pointer().segment;
        reg.di = dos_data.dos_pointer().offset;
        reg.call_int(0x10);

        // If this fails, VBE is not supported.
        if (reg.ax != 0x004f) return nullptr;

        // Set up the info block.
        bios_info.vbe_signature.assign(ptr->vbe_signature, ptr->vbe_signature + 4);
        bios_info.vbe_version = ptr->vbe_version;
        bios_info.capabilities = std::bit_cast<decltype(vbe_info::capabilities)>(ptr->capabilities);
        bios_info.total_memory = ptr->total_memory * 64_KB;

        {
            dpmi::mapped_dos_memory<const char> str { 256, ptr->oem_string };
            bios_info.oem_string = str.near_pointer();
        }

        // If this is a VBE 1.x implementation, we're done now.
        if (ptr->vbe_version < 0x0200)
        {
            instance_ptr = new (&instance) vbe;
            goto done;
        }

        // Continue setting up info block with VBE 2+ data.
        std::copy_n(ptr->oem_data, 256, bios_info.oem_data.data());
        bios_info.oem_software_version = ptr->oem_software_ver;
        {
            dpmi::mapped_dos_memory<const char> str { 256, ptr->oem_vendor_name };
            bios_info.oem_vendor_name = str.near_pointer();
        }
        {
            dpmi::mapped_dos_memory<const char> str { 256, ptr->oem_product_name };
            bios_info.oem_product_name = str.near_pointer();
        }
        {
            dpmi::mapped_dos_memory<const char> str { 256, ptr->oem_product_version };
            bios_info.oem_product_version = str.near_pointer();
        }

        // Now check for the VBE 2.0 protected-mode interface.  This may not
        // be present in VBE 3.0.
        reg.ax = 0x4f0a;
        reg.bl = 0;
        reg.call_int(0x10);
        if (reg.ax == 0x004f)
        {
            dpmi::mapped_dos_memory<const std::byte> pm_table { reg.cx, dpmi::far_ptr16 { reg.es, reg.di } };
            const std::byte* const pm_table_ptr = pm_table.near_pointer();
            vbe2_pm_interface.assign(pm_table_ptr, pm_table_ptr + reg.cx);
            auto cs_limit = reinterpret_cast<std::size_t>(vbe2_pm_interface.data() + reg.cx);
            if (dpmi::descriptor::get_limit(dpmi::get_cs()) < cs_limit)
                dpmi::descriptor::set_limit(dpmi::get_cs(), cs_limit);

            {
                auto* ptr = reinterpret_cast<std::uint16_t*>(vbe2_pm_interface.data());
                vbe2_call_set_window = reinterpret_cast<std::uintptr_t>(ptr) + ptr[0];
                vbe2_call_set_display_start = reinterpret_cast<std::uintptr_t>(ptr) + ptr[1];
                vbe2_call_set_palette = reinterpret_cast<std::uintptr_t>(ptr) + ptr[2];

                auto* io_list = reinterpret_cast<std::uint16_t*>(reinterpret_cast<std::uintptr_t>(ptr) + *(ptr + 3));
                if (*io_list != 0)
                {
                    while (*io_list != 0xffff) ++io_list;
                    if (*++io_list != 0xffff)
                    {
                        auto* addr = reinterpret_cast<std::uintptr_t*>(io_list);
                        auto* size = io_list + 2;
                        vbe2_mmio_memory.emplace(*size, *addr);
                        vbe2_mmio.emplace(vbe2_mmio_memory->create_segment());
                    }
                }
            }
            vbe2_pm = true;
        }

        if (ptr->vbe_version < 0x0300)
        {
            instance_ptr = new (&instance) vbe2;
            goto done;
        }

        // We have a VBE 3.0 interface.
        instance_ptr = new (&instance) vbe3;

        // Now set up the (optional) VBE 3.0 protected-mode interface.
        try
        {
            std::size_t bios_size;
            {
                dpmi::mapped_dos_memory<const std::byte> video_bios_remap { 128_KB, dpmi::far_ptr16 { 0xC000, 0 } };
                const std::byte* ptr = video_bios_remap.near_pointer();
                bios_size = reinterpret_cast<const std::uint8_t*>(ptr)[2] * 512;
                video_bios_memory.reset(new std::byte[bios_size]);
                std::copy_n(ptr, bios_size, video_bios_memory.get());
            }
            {
                char* const begin = reinterpret_cast<char*>(video_bios_memory.get());
                char* const end = begin + bios_size;
                const char* const search_value = "PMID";
                auto* const pos = std::search(begin, end, search_value, search_value + 4);
                if (pos == end) goto done;
                pmid = reinterpret_cast<vbe3_pm_info*>(pos);
                if (checksum8(*pmid) != 0) goto done;
            }

            pmid->in_protected_mode = true;

            fake_bda_memory.reset(new std::byte[2_KB] { });
            fake_bda.emplace(dpmi::linear_memory::from_pointer(fake_bda_memory.get(), 2_KB).create_segment());
            auto segdata = fake_bda->read();
            segdata.segment.is_32_bit = false;
            fake_bda->write(segdata);
            pmid->bda_selector = fake_bda->get_selector();

            video_bios.emplace(dpmi::linear_memory::from_pointer(video_bios_memory.get(), bios_size).create_segment());
            segdata = video_bios->read();
            segdata.segment.is_32_bit = false;
            video_bios->write(segdata);
            pmid->data_selector = video_bios->get_selector();

            video_bios_code.emplace(dpmi::descriptor::create());
            segdata.segment.code_segment.is_code_segment = true;
            video_bios_code->write(segdata);

            vbe3_stack_memory.reset(new std::byte[4_KB]);
            vbe3_stack.emplace(dpmi::linear_memory::from_pointer(vbe3_stack_memory.get(), 4_KB).create_segment());
            segdata = vbe3_stack->read();
            segdata.segment.is_32_bit = false;
            vbe3_stack->write(segdata);
            vbe3_stack_ptr = { vbe3_stack->get_selector(), 4_KB - 2 };
            vbe3_entry_point = { video_bios_code->get_selector(), pmid->init_entry_point };

            pmid->a000_selector = dpmi::dos_selector(0xa000);
            pmid->b000_selector = dpmi::dos_selector(0xb000);
            pmid->b800_selector = dpmi::dos_selector(0xb800);

            asm volatile ("call vbe3" ::: "eax", "ebx", "ecx", "edx", "esi", "edi", "cc");

            vbe3_entry_point.offset = pmid->entry_point;
            vbe3_pm = true;
        }
        catch (...)
        {
            vbe3_stack.reset();
            video_bios.reset();
            fake_bda.reset();
            vbe3_stack_memory.reset();
            video_bios_memory.reset();
            fake_bda_memory.reset();
            throw;
        }

    done:
        // Now see what video modes we have.
        dpmi::mapped_dos_memory<std::uint16_t> far_mode_list { 256, ptr->video_mode_list };
        auto get_mode = [&](std::uint16_t num)
        {
            dos_data->mode = { };
            reg.ax = 0x4f01;
            reg.cx = num;
            reg.es = dos_data.dos_pointer().segment;
            reg.di = dos_data.dos_pointer().offset;
            reg.call_int(0x10);
            if (reg.ax == 0x004f)
                mode_list[num] = dos_data->mode;
        };

        for (auto* p = far_mode_list.near_pointer(); *p != 0xffff; ++p)
            get_mode(*p);

        // Also see if there is any info on regular VGA modes.
        for (auto n = 0; n < 0x80; ++n)
            get_mode(n);

        return instance_ptr;
    }

    const vbe_info& vbe::info()
    {
        return bios_info;
    }

    const std::map<std::uint_fast16_t, vbe_mode_info>& vbe::modes()
    {
        return mode_list;
    }

    std::size_t vbe::lfb_size_in_pixels()
    {
        auto r = scanline_length();
        return r.pixels_per_scanline * mode_info->resolution.y * mode_info->lfb_num_image_pages;
    }

    std::size_t vbe::bits_per_pixel()
    {
        auto r = scanline_length();
        return r.bytes_per_scanline * 8 / r.pixels_per_scanline;
    }

    void vbe::set_mode(vbe_mode m, const crtc_info*)
    {
        auto& reg = get_realmode_registers();
        reg.ax = 0x4f02;
        reg.bx = m.mode;
        reg.call_int(0x10);
        check_error(reg.ax, __PRETTY_FUNCTION__);
        mode = m;
        mode_info = &mode_list[m.index];
        dac_bits = 6;
    }

    void vbe3::set_mode(vbe_mode m, const crtc_info* crtc)
    {
        if (crtc == nullptr) m.use_custom_crtc_timings = false;
        auto& reg = get_realmode_registers();
        reg.ax = 0x4f02;
        reg.bx = m.mode;
        if (m.use_custom_crtc_timings)
        {
            auto& dos_data = get_dos_data();
            dos_data->crtc = *crtc;
            reg.es = dos_data.dos_pointer().segment;
            reg.di = dos_data.dos_pointer().offset;
            reg.call_int(0x10);
        }
        else reg.call_int(0x10);
        check_error(reg.ax, __PRETTY_FUNCTION__);
        mode = m;
        mode_info = &mode_list[m.index];
        dac_bits = 6;
    }

    scanline_info vbe::scanline_length(std::size_t width, bool width_in_pixels)
    {
        auto& reg = get_realmode_registers();
        reg.ax = 0x4f06;
        reg.bl = width_in_pixels ? 0 : 2;
        reg.cx = width;
        reg.call_int(0x10);
        check_error(reg.ax, __PRETTY_FUNCTION__);
        return { reg.cx, reg.bx, reg.dx };
    }

    scanline_info vbe::scanline_length()
    {
        auto& reg = get_realmode_registers();
        reg.ax = 0x4f06;
        reg.bl = 1;
        reg.call_int(0x10);
        check_error(reg.ax, __PRETTY_FUNCTION__);
        return { reg.cx, reg.bx, reg.dx };
    }

    scanline_info vbe::max_scanline_length()
    {
        auto& reg = get_realmode_registers();
        reg.ax = 0x4f06;
        reg.bl = 3;
        reg.call_int(0x10);
        check_error(reg.ax, __PRETTY_FUNCTION__);
        return { reg.cx, reg.bx, reg.dx };
    }

    void vbe::display_start(vector2i pos, bool wait_for_vsync)
    {
        auto& reg = get_realmode_registers();
        reg.ax = 0x4f07;
        reg.bx = wait_for_vsync ? 0x80 : 0;
        reg.cx = pos.x();
        reg.dx = pos.y();
        reg.call_int(0x10);
        check_error(reg.ax, __PRETTY_FUNCTION__);
    }

    void vbe2::display_start(vector2i pos, bool wait_for_vsync)
    {
        if (!vbe2_pm) return vbe::display_start(pos, wait_for_vsync);

        auto bps = (mode.use_lfb_mode && info().vbe_version >= 0x300) ? mode_info->lfb_bytes_per_scanline : mode_info->bytes_per_scanline;
        auto start = pos.x() * (mode_info->bits_per_pixel / 8) + pos.y() * bps;
        if (mode_info->bits_per_pixel >= 8) start = ((start & 3) << 30 | (start >> 2));
        split_uint32_t split_start { start };

        dpmi::selector mmio = vbe2_mmio ? vbe2_mmio->get_selector() : dpmi::get_ds();
        std::uint16_t ax;
        force_frame_pointer();
        asm volatile(
            "push es;"
            "mov es, %k2;"
            "call %1;"
            "pop es;"
            : "=a" (ax)
            : "rm" (vbe2_call_set_display_start)
            , "r" (mmio)
            , "a" (0x4f07)
            , "b" (wait_for_vsync ? 0x80 : 0)
            , "c" (split_start.lo)
            , "d" (split_start.hi)
            : "cc");
    }

    void vbe3::display_start(vector2i pos, bool wait_for_vsync)
    {
        if (not vbe3_pm) return vbe2::display_start(pos, wait_for_vsync);

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

    vector2i vbe::display_start()
    {
        auto& reg = get_realmode_registers();
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
        return display_start(pos, false);
    }

    void vbe3::schedule_display_start(vector2i pos)
    {
        if (not mode_info->attr.triple_buffering_supported) return display_start(pos);

        auto bps = mode.use_lfb_mode ? mode_info->lfb_bytes_per_scanline : mode_info->bytes_per_scanline;
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
            auto& reg = get_realmode_registers();
            reg.ax = 0x4f07;
            reg.bx = 2;
            reg.ecx = start;
            reg.call_int(0x10);
            check_error(reg.ax, __PRETTY_FUNCTION__);
        }
    }

    bool vbe::scheduled_display_start_status()
    {
        return true;
    }

    bool vbe3::scheduled_display_start_status()
    {
        if (!mode_info->attr.triple_buffering_supported) return vbe2::scheduled_display_start_status();

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
            auto& reg = get_realmode_registers();
            reg.ax = 0x4f07;
            reg.bx = 4;
            reg.call_int(0x10);
            check_error(reg.ax, __PRETTY_FUNCTION__);
            return reg.cx != 0;
        }
    }

    std::uint8_t vbe::palette_format(std::uint8_t bits_per_channel)
    {
        auto& reg = get_realmode_registers();
        reg.ax = 0x4f08;
        reg.bh = bits_per_channel;
        reg.call_int(0x10);
        check_error(reg.ax, __PRETTY_FUNCTION__);
        dac_bits = reg.bh;
        return reg.bh;
    }

    std::uint8_t vbe3::palette_format(std::uint8_t bits_per_channel)
    {
        if (not vbe3_pm) return vbe2::palette_format(bits_per_channel);

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

    std::uint8_t vbe::palette_format()
    {
        auto& reg = get_realmode_registers();
        reg.ax = 0x4f08;
        reg.bx = 1;
        reg.call_int(0x10);
        check_error(reg.ax, __PRETTY_FUNCTION__);
        dac_bits = reg.bh;
        return reg.bh;
    }

    void vbe2::palette(std::span<const px32n> pal, std::size_t first, bool wait_for_vsync)
    {
        const auto size = std::min(pal.size(), 256ul);
        if (vbe2_pm)
        {
            std::array<pxvga, 256> copy;
            auto* ptr = static_cast<const void*>(pal.data());
            if (dac_bits < 8)
            {
                mmx_function<default_simd()>([out = copy.begin(), size, pal]<simd flags>()
                {
                    simd_pipeline pipe { simd_source { }, px_convert<pxvga>, simd_sink { out } };
                    for (auto p = pal.begin(); p != pal.end();)
                        simd_run<flags>(pipe, &p);
                });
                ptr = static_cast<const void*>(copy.data());
            }

            dpmi::selector mmio = vbe2_mmio ? vbe2_mmio->get_selector() : dpmi::get_ds();
            force_frame_pointer();
            asm volatile(
                "push ds;"
                "mov ds, %k1;"
                "call cs:%0;"
                "pop ds;"
                :: "m" (vbe2_call_set_palette)
                , "r" (mmio)
                , "a" (0x4f09)
                , "b" (wait_for_vsync ? 0x80 : 0)
                , "c" (size)
                , "d" (first)
                , "D" (ptr)
                : "cc");
        }
        else
        {
            auto& dos_data = get_dos_data();
            if (dac_bits < 8)
            {
                mmx_function<default_simd()>([out = dos_data->palette.data(), size, pal]<simd flags>()
                {
                    simd_pipeline pipe { simd_source { }, px_convert<pxvga>, simd_sink { reinterpret_cast<pxvga*>(out) } };
                    for (auto p = pal.begin(); p != pal.end();)
                        simd_run<flags>(pipe, &p);
                });
            }
            else
            {
                for (std::size_t i = 0; i < size; ++i)
                    new(dos_data->palette.data() + i) px32n { std::move(pal[i]) };
            }

            auto& reg = get_realmode_registers();
            reg.ax = 0x4f09;
            reg.bx = wait_for_vsync ? 0x80 : 0;
            reg.cx = size;
            reg.dx = first;
            reg.es = dos_data.dos_pointer().segment;
            reg.di = dos_data.dos_pointer().offset;
            reg.call_int(0x10);
            check_error(reg.ax, __PRETTY_FUNCTION__);
        }
    }

    void vbe3::palette(std::span<const px32n> pal, std::size_t first, bool wait_for_vsync)
    {
        if (not vbe3_pm) return vbe2::palette(pal, first, wait_for_vsync);

        const auto size = std::min(pal.size(), 256ul);
        std::array<pxvga, 256> copy;
        auto* ptr = static_cast<const void*>(pal.data());
        if (dac_bits < 8)
        {
            mmx_function<default_simd()>([out = copy.data(), pal, size]<simd flags>()
            {
                simd_pipeline pipe { simd_source { }, px_convert<pxvga>, simd_sink { out } };
                for (auto p = pal.begin(); p != pal.end();)
                    simd_run<flags>(pipe, &p);
            });
            ptr = static_cast<const void*>(copy.data());
        }

        std::uint16_t ax { 0x4f09 };
        force_frame_pointer();
        asm volatile(
            "call vbe3"
            : "+a" (ax)
            : "b" (wait_for_vsync ? 0x80 : 0)
            , "c" (size)
            , "d" (first)
            , "D" (ptr)
            : "esi", "cc");
        check_error(ax, __PRETTY_FUNCTION__);
    }

    std::array<px32n, 256> vbe2::palette()
    {
        auto& dos_data = get_dos_data();

        auto& reg = get_realmode_registers();
        reg.ax = 0x4f09;
        reg.bx = 1;
        reg.cx = 256;
        reg.dx = 0;
        reg.es = dos_data.dos_pointer().segment;
        reg.di = dos_data.dos_pointer().offset;
        reg.call_int(0x10);
        if (info().vbe_version < 0x300) check_error(reg.ax, __PRETTY_FUNCTION__);
        else if (reg.ax != 0x004f) return vga::palette();

        std::array<px32n, 256> result;
        auto* ptr = dos_data->palette.data();
        if (dac_bits < 8)
        {
            mmx_function<default_simd()>([out = result.data(), ptr]<simd flags>()
            {
                simd_pipeline pipe { simd_in, px_convert<px32n>, simd_out };
                for (auto i = 0; i < 256; ++i)
                    out[i] = simd_run<flags>(pipe, reinterpret_cast<const pxvga*>(ptr)[i]);
            });
        }
        else for (auto i = 0; i < 256; ++i) result[i] = ptr[i];
        return result;
    }

    std::uint32_t vbe3::closest_pixel_clock(std::uint32_t desired_clock, std::uint16_t mode_num)
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
            auto& reg = get_realmode_registers();
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
