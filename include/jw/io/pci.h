/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2017 - 2023 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <initializer_list>
#include <stdexcept>
#include <jw/io/ioport.h>
#include <jw/common.h>

namespace jw::io
{
    struct pci_device
    {
        struct error : public std::runtime_error { using runtime_error::runtime_error; };
        struct unsupported_function : public error { using error::error; };
        struct bad_register : public error { using error::error; };
        struct device_not_found : public error { using error::error; };

    protected:
        struct device_tag { };
        struct class_tag { };
        pci_device(device_tag, std::uint16_t vendor, std::initializer_list<std::uint16_t> devices, std::uint8_t function = 0xff);
        pci_device(class_tag, std::uint8_t class_code, std::initializer_list<std::uint8_t> subclass_codes, std::uint8_t interface_type);
        virtual ~pci_device();

        pci_device(pci_device&&) = delete;
        pci_device(const pci_device&) = delete;
        pci_device& operator=(pci_device&&) = delete;
        pci_device& operator=(const pci_device&) = delete;

        template<typename T>
        struct pci_register
        {
            static_assert(sizeof(T) == 4, "PCI registers must be 32 bits wide.");
            pci_register(const pci_device* dev, std::uint8_t reg) noexcept : regnum { get_regnum(dev, reg) } { }

            auto read() const noexcept
            {
                index.write(regnum);
                return data.read();
            }

            void write(const T& value) const noexcept
            {
                index.write(regnum);
                data.write(value);
            }

        private:
            static std::uint32_t get_regnum(const pci_device* dev, std::uint8_t reg) noexcept;

            const std::uint32_t regnum;
            static constexpr out_port<std::uint32_t> index { 0xcf8 };
            static constexpr io_port<T> data { 0xcfc };
        };

        struct [[gnu::packed]] reg_command
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

        struct [[gnu::packed]] reg_status
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

        struct [[gnu::packed]] reg_id { std::uint16_t vendor, device; };
        struct [[gnu::packed]] reg_command_and_status { reg_command command; reg_status status; };
        struct [[gnu::packed]] reg_type { std::uint8_t revision, prog_interface, subclass, class_code; };
        struct [[gnu::packed]] reg_bus_info { std::uint8_t irq, interrupt_pin, min_grant, max_latency; };
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

        auto id()                   const noexcept { return pci_register<reg_id>                 { this, 0x00 }; }
        auto command_and_status()   const noexcept { return pci_register<reg_command_and_status> { this, 0x04 }; }
        auto type()                 const noexcept { return pci_register<reg_type>               { this, 0x08 }; }
        auto misc()                 const noexcept { return pci_register<reg_misc>               { this, 0x0C }; }
        auto base0()                const noexcept { return pci_register<std::uintptr_t>         { this, 0x10 }; }
        auto base1()                const noexcept { return pci_register<std::uintptr_t>         { this, 0x14 }; }
        auto base2()                const noexcept { return pci_register<std::uintptr_t>         { this, 0x18 }; }
        auto base3()                const noexcept { return pci_register<std::uintptr_t>         { this, 0x1C }; }
        auto base4()                const noexcept { return pci_register<std::uintptr_t>         { this, 0x20 }; }
        auto base5()                const noexcept { return pci_register<std::uintptr_t>         { this, 0x24 }; }
        auto cardbus_info()         const noexcept { return pci_register<std::uintptr_t>         { this, 0x28 }; }
        auto subsystem_id()         const noexcept { return pci_register<reg_id>                 { this, 0x2C }; }
        auto expansion_rom_base()   const noexcept { return pci_register<std::uintptr_t>         { this, 0x30 }; }
        auto capabilities_list()    const noexcept { return pci_register<std::uint32_t>          { this, 0x34 }; }
        auto bus_info()             const noexcept { return pci_register<reg_bus_info>           { this, 0x3C }; }

        auto read_status() { return command_and_status().read().status; }
        void clear_status(reg_status clear_bits)
        {
            auto s = command_and_status().read();
            s.status = clear_bits;
            command_and_status().write(s);
        }

        auto current_command() { return command_and_status().read().command; }
        void send_command(reg_command cmd)
        {
            reg_command_and_status r { };
            r.command = cmd;
            command_and_status().write(r);
        }

    private:
        std::uint8_t bus, device, function;
    };

    template <typename T>
    std::uint32_t pci_device::pci_register<T>::get_regnum(const pci_device* dev, std::uint8_t reg) noexcept
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
        x.function = dev->function;
        x.device = dev->device;
        x.bus = dev->bus;
        x.enable_config = true;
        return x.value;
    }
}
