/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstdint>
#include <string>
#include <map>
#include <jw/dpmi/alloc.h>

namespace jw::dpmi
{
    struct cpuid_leaf
    {
        std::uint32_t eax, ebx, ecx, edx;
    };

    struct cpu_feature_flags
    {
        bool fpu_on_chip : 1;
        bool v86_mode_enhancements : 1;
        bool debugging_extensions : 1;
        bool page_size_extension : 1;
        bool time_stamp_counter : 1;
        bool model_specific_registers : 1;
        bool physical_address_extension : 1;
        bool machine_check_exception : 1;
        bool cmpxchg8b : 1;
        bool apic_on_chip : 1;
        bool : 1;
        bool sysenter : 1;
        bool memory_type_range_registers : 1;
        bool page_global_bit : 1;
        bool machine_check_architecture : 1;
        bool cmov : 1;
        bool page_attribute_table : 1;
        bool page_size_extension_36bit : 1;
        bool processor_serial_number : 1;
        bool clflush : 1;
        bool : 1;
        bool debug_store : 1;
        bool acpi : 1;
        bool mmx : 1;
        bool fxsave : 1;
        bool sse : 1;
        bool sse2 : 1;
        bool self_snoop : 1;
        bool hyperthreading : 1;
        bool thermal_monitor : 1;
        bool : 1;
        bool pending_break_enable : 1;
    };

    struct cpuid
    {
        [[gnu::const]] static bool supported() noexcept
        {
            enum : std::uint8_t { unknown, yes, no } static status;
            if (status == unknown) [[unlikely]]
            {
                if (check_support()) status = yes;
                else status = no;
            }
            return status == yes;
        }

        static const cpuid_leaf& leaf(std::uint32_t i)
        {
            if (leaves.empty()) [[unlikely]] populate();
            return leaves[i];
        }

        static std::string vendor()
        {
            std::string v { };
            v.reserve(3 * 4);
            auto& l = leaf(0);
            v.append(reinterpret_cast<const char*>(&l.ebx), 4);
            v.append(reinterpret_cast<const char*>(&l.edx), 4);
            v.append(reinterpret_cast<const char*>(&l.ecx), 4);
            return v;
        }

        static cpu_feature_flags feature_flags()
        {
            union
            {
                std::uint32_t edx = leaf(1).edx;
                cpu_feature_flags flags;
            };
            return flags;
        }

    private:
        static inline std::map<std::uint32_t, cpuid_leaf, std::less<std::uint32_t>, locking_allocator<std::pair<const std::uint32_t, cpuid_leaf>>> leaves { };

        static void populate();

        [[gnu::const]] static bool check_support() noexcept;
    };
}
