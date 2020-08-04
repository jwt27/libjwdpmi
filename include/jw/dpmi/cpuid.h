/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <cstdint>
#include <string>
#include <map>
#include <jw/dpmi/alloc.h>

namespace jw::dpmi
{
    struct cpuid
    {
        struct leaf_t
        {
            std::uint32_t eax, ebx, ecx, edx;
        };

        struct feature_flags_t
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

        static const leaf_t& leaf(std::uint32_t i)
        {
            if (leaves.empty()) [[unlikely]] populate();
            return leaves[i];
        }

        static std::string vendor()
        {
            std::string v { };
            auto& l = leaf(0);
            v.append(reinterpret_cast<const char*>(&l.ebx), 4);
            v.append(reinterpret_cast<const char*>(&l.edx), 4);
            v.append(reinterpret_cast<const char*>(&l.ecx), 4);
            return v;
        }

        static feature_flags_t feature_flags()
        {
            union
            {
                std::uint32_t edx = leaf(1).edx;
                feature_flags_t flags;
            };
            return flags;
        }

    private:
        static inline std::map<std::uint32_t, leaf_t, std::less<std::uint32_t>, locking_allocator<std::pair<const std::uint32_t, leaf_t>>> leaves { };

        static void populate();
    };
}
