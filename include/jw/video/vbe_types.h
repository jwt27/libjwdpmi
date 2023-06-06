/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2023 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <string>
#include <jw/dpmi/dpmi.h>

namespace jw::video::detail
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpacked-not-aligned"
    struct [[gnu::packed]] vbe3_pm_info
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
#pragma GCC diagnostic pop
}

namespace jw::video
{
    struct vbe_info
    {
        std::string vbe_signature;
        std::uint16_t vbe_version;
        std::string oem_string;
        struct
        {
            bool dac_is_8bit : 1;           // 6-bit otherwise
            // VBE 2.0: //
            bool is_not_vga_compatible : 1;
            bool use_snow_checking : 1;     // set blank bit on function 09h
            // VBE 3.0: //
            bool stereo_supported : 1;
            bool stereo_via_vesa_evc : 1;   // "VESA EVC" if set, "external VESA" otherwise
            unsigned : 27;
        } capabilities;
        // VBE 1.1: //
        std::uint16_t total_memory;         // number of 64k blocks
        // VBE 2.0: //
        std::uint16_t oem_software_ver;
        std::string oem_vendor_name;
        std::string oem_product_name;
        std::string oem_product_version;
        std::array<byte, 256> oem_data;
    };

    struct alignas(4) [[gnu::packed]] vbe_mode_info
    {
        struct [[gnu::packed]]
        {
            bool is_supported : 1;
            unsigned : 1;
            bool tty_supported : 1;
            bool is_color_mode : 1;
            bool is_graphics_mode : 1;
            // VBE 1.1: //
            bool is_not_vga_compatible : 1;
            bool windowed_mode_not_available : 1;
            bool lfb_mode_available : 1;
            // VBE 3.0: //
            bool double_scan_available : 1;
            bool interlaced_available : 1;
            bool triple_buffering_supported : 1;
            bool stereo_supported : 1;
            bool dual_display_supported : 1;
            unsigned : 3;
        } attr;
        struct [[gnu::packed]]
        {
            bool relocatable_windows_supported : 1;
            bool is_readable : 1;
            bool is_writeable : 1;
            unsigned : 5;
        } winA_attr, winB_attr;
        std::uint16_t win_granularity;
        std::uint16_t win_size;
        std::uint16_t winA_segment;
        std::uint16_t winB_segment;
        dpmi::far_ptr16 win_function_ptr;
        std::uint16_t bytes_per_scanline;
        // VBE 1.1: // (optional in 1.0)
        std::uint16_t resolution_x;
        std::uint16_t resolution_y;
        std::uint8_t char_size_x;
        std::uint8_t char_size_y;
        std::uint8_t num_planes;
        std::uint8_t bits_per_pixel;
        std::uint8_t num_banks;
        enum
        {
            text = 0,
            cga = 1,
            hercules = 2,
            planar = 3,
            packed_pixel = 4,
            non_chain_4 = 5,
            direct = 6,
            yuv = 7
            // 08-0F = reserved by VESA
            // 10-FF = OEM defined
        } memory_model : 8;
        std::uint8_t bank_size;
        // VBE 1.2: //
        std::uint8_t num_image_pages;
        unsigned : 8;
        std::uint8_t red_mask_size;
        std::uint8_t red_field_pos;
        std::uint8_t green_mask_size;
        std::uint8_t green_field_pos;
        std::uint8_t blue_mask_size;
        std::uint8_t blue_field_pos;
        std::uint8_t reserved_mask_size;
        std::uint8_t reserved_field_pos;
        struct [[gnu::packed]]
        {
            bool color_ramp_is_programmable : 1;
            bool reserved_bits_are_usable : 1;
            unsigned : 6;
        } direct_color_mode_info;
        // VBE 2.0: //
        std::uintptr_t physical_base_ptr;
        unsigned : 32;
        unsigned : 16;
        // VBE 3.0: //
        std::uint16_t linear_bytes_per_scanline;
        std::uint8_t banked_num_image_pages;
        std::uint8_t linear_num_image_pages;
        std::uint8_t linear_red_mask_size;
        std::uint8_t linear_red_field_pos;
        std::uint8_t linear_green_mask_size;
        std::uint8_t linear_green_field_pos;
        std::uint8_t linear_blue_mask_size;
        std::uint8_t linear_blue_field_pos;
        std::uint8_t linear_reserved_mask_size;
        std::uint8_t linear_reserved_field_pos;
        std::uint32_t max_pixel_clock;
        byte _reserved[190]; // HACK: should be 189?
    };

    struct [[gnu::packed]] crtc_info
    {
        std::uint16_t h_total;
        std::uint16_t h_sync_start;
        std::uint16_t h_sync_end;
        std::uint16_t v_total;
        std::uint16_t v_sync_start;
        std::uint16_t v_sync_end;
        struct [[gnu::packed]]
        {
            bool double_scan : 1;
            bool interlaced : 1;
            bool neg_hsync_polarity : 1;
            bool neg_vsync_polarity : 1;
            unsigned : 4;
        } flags;
        std::uint32_t pixel_clock;  // in 1Hz units
        std::uint16_t refresh_rate; // in 0.01Hz units
        byte _reserved[40];
    };

    union alignas(2) [[gnu::packed]] vbe_mode
    {
        struct [[gnu::packed]]
        {
            unsigned mode : 11;
            bool use_custom_crtc_timings : 1;
            unsigned : 2;
            bool use_lfb_mode : 1;
            bool dont_clear_video_memory : 1;
        };
        std::uint16_t raw_value;

        constexpr vbe_mode() : raw_value(0) { }
        constexpr vbe_mode(std::uint16_t num) : raw_value(num) { }
    };

    static_assert (sizeof(detail::raw_vbe_info) == 0x200);
    static_assert (sizeof(vbe_mode_info) == 0x100);
    static_assert (sizeof(crtc_info) == 0x3b);
    static_assert (sizeof(vbe_mode) == 0x2);
}
