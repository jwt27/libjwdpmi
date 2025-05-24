/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2019 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <cstdint>
#include <cstring>
#include <string_view>
#include <bit>

namespace jw::dpmi
{
    struct cpuid_leaf
    {
        std::uint32_t eax, ebx, edx, ecx;
    };

    struct common_cpu_feature_flags
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
    };

    struct alignas(int) intel_cpu_feature_flags : common_cpu_feature_flags
    {
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

    struct alignas(int) amd_cpu_feature_flags : common_cpu_feature_flags
    {
        bool page_attribute_table : 1;
        bool page_size_extension_36bit : 1;
        bool : 2;
        bool execute_disable : 1;
        bool : 1;
        bool mmx_extensions : 1;
        bool mmx : 1;
        bool fxsave : 1;
        bool fast_fxsave : 1;
        bool : 1;
        bool rdtscp : 1;
        bool : 1;
        bool long_mode : 1;
        bool amd3dnow_extensions : 1;
        bool amd3dnow : 1;
    };

    static_assert(sizeof(intel_cpu_feature_flags) == 4);
    static_assert(sizeof(amd_cpu_feature_flags) == 4);

    struct cpuid
    {
        // Check if the CPUID instruction is supported.
        [[gnu::const]] static bool supported() noexcept
        {
            return max() != 0;
        }

        // Returns the maximum allowed parameter to leaf().  A value of 0
        // indicates that CPUID is not supported.
        [[gnu::const]] static std::uint32_t max() noexcept
        {
            return max_leaf;
        }

        // Returns the maximum allowed parameter to extended_leaf().  A value
        // of 0 indicates that extended leaves are not supported.
        [[gnu::const]] static std::uint32_t max_extended() noexcept
        {
            return max_extended_leaf;
        }

        // Get the CPU vendor identification string.  Returns an empty string
        // if CPUID is not supported.
        [[gnu::const]] static std::string_view vendor()
        {
            if (not supported())
                return { };

            static char buf[12];
            const auto l = leaf(0);
            std::memcpy(buf + 0, &l.ebx, 4);
            std::memcpy(buf + 4, &l.edx, 4);
            std::memcpy(buf + 8, &l.ecx, 4);
            return { buf, sizeof(buf) };
        }

        // Get the feature flags from leaf(1).edx.  If CPUID is not supported,
        // all bits will be clear.
        [[gnu::const]] static intel_cpu_feature_flags feature_flags() noexcept
        {
            if (max() > 0) [[likely]]
                return std::bit_cast<intel_cpu_feature_flags>(leaf(1).edx);
            else
                return { };
        }

        // Get the feature flags from extended_leaf(1).edx.  If these are not
        // available, all bits will be clear.
        [[gnu::const]] static amd_cpu_feature_flags amd_feature_flags() noexcept
        {
            if (max_extended() > 0) [[likely]]
                return std::bit_cast<amd_cpu_feature_flags>(extended_leaf(1).edx);
            else
                return { };
        }

        // Get the specified CPUID leaf.  Make sure to check max() or
        // supported() before calling this.
        [[gnu::const]] static cpuid_leaf leaf(std::uint32_t i)
        {
            cpuid_leaf l;
            asm volatile ("cpuid" : "=a" (l.eax), "=b" (l.ebx), "=c" (l.ecx), "=d" (l.edx) : "a" (i));
            return l;
        }

        // Get the specified extended CPUID leaf.  You don't need to set the
        // high bit on the index.  Make sure to check max_extended() before
        // calling this.
        [[gnu::const]] static cpuid_leaf extended_leaf(std::uint32_t i)
        {
            return leaf(i | 0x80000000);
        }

        // This is used once during initialization.  No need to call it
        // manually.
        static void setup() noexcept
        {
            bool have_cpuid;
            std::uint32_t scratch;
            asm volatile
            (R"(
                pushfd
                mov %0, [esp]
                xor dword ptr [esp], 0x00200000     # ID bit
                popfd
                pushfd
                cmp %0, [esp]
                pop %0
             )" : "=&r" (scratch)
                , "=@ccne" (have_cpuid)
            );
            if (not have_cpuid) return;
            max_leaf = leaf(0).eax;
            max_extended_leaf = std::max(extended_leaf(0).eax, 0x80000000ul) & 0x7fffffffu;
        }

    private:
        static inline std::uint32_t max_leaf { 0 };
        static inline std::uint32_t max_extended_leaf { 0 };
    };
}
