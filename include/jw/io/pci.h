/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <initializer_list>
#include <unordered_map>
#include <jw/dpmi/realmode.h>
#include <jw/common.h>

namespace jw
{
    namespace io
    {
        struct pci_device
        {
            struct error : public std::runtime_error { using runtime_error::runtime_error; };
            struct unsupported_function : public error { using error::error; };
            struct bad_register : public error { using error::error; };
            struct device_not_found : public error { using error::error; };

        protected:
            pci_device(std::uint16_t vendor, std::initializer_list<std::uint16_t> devices, std::uint8_t function = 0xff);
            virtual ~pci_device();

            template<typename T, std::uint8_t reg>
            struct pci_register
            {
                static_assert(sizeof(T) == 4 && (reg % 4) == 0, "PCI registers must be 32 bits wide.");
                constexpr pci_register(pci_device* device) : dev(*device) { }

                auto read()
                {
                    index.write(get_regnum());
                    return data.read();
                }

                void write(const T& value)
                {
                    index.write(get_regnum());
                    data.write(value);
                }

            private:
                std::uint32_t get_regnum()
                {
                    union
                    {
                        struct
                        {
                            unsigned register_num : 8;
                            unsigned function : 3;
                            unsigned device : 5;
                            unsigned bus : 8;
                            unsigned : 7;
                            bool enable_config : 1;
                        };
                        std::uint32_t value { };
                    } x;
                    x.register_num = reg;
                    x.function = dev.function;
                    x.device = dev.bus_device;
                    x.bus = dev.bus;
                    x.enable_config = true;
                    return x.value;
                }

                pci_device& dev { };
                static constexpr out_port<std::uint32_t> index { 0xcf8 };
                static constexpr io_port<T> data { 0xcfc };
            };

            struct reg_id { std::uint16_t vendor, device; };
            struct [[gnu::packed]] reg_status
            {
                struct [[gnu::packed]]
                {
                    bool io_access : 1;
                    bool memory_access : 1;
                    bool bus_master : 1;
                    bool respond_to_special_cycle : 1;
                    bool enable_memory_write_and_invalidate : 1;
                    bool vga_palette_snoop : 1;
                    bool respond_to_parity_error : 1;
                    bool enable_stepping : 1;               // not used since PCI 3.0
                    bool enable_system_error : 1;
                    bool enable_fast_back_to_back : 1;
                    bool disable_interrupt : 1;
                    unsigned : 5;
                } command;
                struct [[gnu::packed]]
                {
                    unsigned : 3;
                    bool interrupt : 1;
                    bool has_capabilities_list : 1;
                    bool is_66mhz_capable : 1;
                    bool user_definable_configuration : 1;   // not used since PCI 2.2
                    bool is_fast_back_to_back_capable : 1;
                    bool master_parity_error : 1;
                    enum { fast, medium, slow } devsel_timing : 2;
                    bool sent_target_abort : 1;
                    bool received_target_abort : 1;
                    bool received_master_abort : 1;
                    bool sent_system_error : 1;
                    bool parity_error : 1;
                } status;
            };
            struct reg_type { std::uint8_t revision, prog_interface, subclass, class_code; };
            struct [[gnu::packed]] reg_misc
            {
                std::uint8_t cache_line_size;
                std::uint8_t latency_timer;
                struct [[gnu::packed]]
                {
                    unsigned header_type : 7;
                    bool multifunction : 1;
                };
                struct [[gnu::packed]]
                {
                    unsigned result : 4;
                    unsigned : 2;
                    bool start : 1;
                    bool is_capable : 1;
                } self_test;
            };

            pci_register<reg_id, 0x00> id { this };
            pci_register<reg_status, 0x04> status { this };
            pci_register<reg_type, 0x08> type { this };
            pci_register<reg_misc, 0x0C> misc { this };
            pci_register<std::uintptr_t, 0x10> base_0 { this };

        private:
            std::uint16_t device_id, vendor_id;
            std::uint8_t bus, bus_device;
            std::uint16_t index;
            std::uint8_t function;

            using map_type = std::unordered_map<std::uint16_t, std::unordered_map<std::uint16_t, std::unordered_map<std::uint16_t, pci_device*>>>;
            // map indexed by: vendor->device->index
            static map_type* device_map;
        };
    }
}
