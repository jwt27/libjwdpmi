/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2016 - 2023 J.W. Jagersma, see COPYING.txt for details    */

#include <string>
#include <fmt/format.h>
#include <jw/dpmi/dpmi_error.h>

using namespace std::literals;

namespace jw::dpmi
{
    static std::string_view msg(int ev) noexcept
    {
        switch (ev)
        {
        case 0x0007: return "Memory configuration blocks damaged"sv;
        case 0x0008: return "Insufficient memory"sv;
        case 0x0009: return "Incorrect memory segment specified"sv;
        case 0x8001: return "Unsupported function"sv;
        case 0x8002: return "Invalid state"sv;
        case 0x8003: return "System integrity"sv;
        case 0x8004: return "Deadlock"sv;
        case 0x8005: return "Request cancelled"sv;
        case 0x8010: return "Resource Unavailable"sv;
        case 0x8011: return "Descriptor unavailable"sv;
        case 0x8012: return "Linear memory unavailable"sv;
        case 0x8013: return "Physical memory unavailable"sv;
        case 0x8014: return "Backing store unavailable"sv;
        case 0x8015: return "Callback unavailable"sv;
        case 0x8016: return "Handle unavailable"sv;
        case 0x8017: return "Lock count exceeded"sv;
        case 0x8018: return "Resource owned exclusively"sv;
        case 0x8019: return "Resource owned shared"sv;
        case 0x8021: return "Invalid value"sv;
        case 0x8022: return "Invalid selector"sv;
        case 0x8023: return "Invalid handle"sv;
        case 0x8024: return "Invalid callback"sv;
        case 0x8025: return "Invalid linear address"sv;
        case 0x8026: return "Invalid request"sv;
        default:     return "Unknown error"sv;
        }
    }

    const std::error_category& dpmi_error_category() noexcept
    {
        struct : public std::error_category
        {
            virtual const char* name() const noexcept override { return "DPMI"; }
            virtual std::string message(int ev) const override
            {
                return fmt::format("DPMI error 0x{:0>4x}: {}.", ev, msg(ev));
            }
        } static cat;
        return cat;
    }
}
