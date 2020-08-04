/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#include <string>
#include <sstream>
#include <jw/dpmi/dpmi_error.h>

using namespace std;

namespace jw
{
    namespace dpmi
    {
        // descriptions from http://www.delorie.com/djgpp/doc/dpmi/api/errors.html
        string dpmi_error_category::message(int ev) const
        {
            switch (ev)
            {
            case 0x0000: return "DPMI Error 0x0000: No error..?";
            case 0x0007: return "DPMI Error 0x0007: Memory configuration blocks damaged: " "The operating system has detected corruption in the real - mode memory arena.";
            case 0x0008: return "DPMI Error 0x0008: Insufficient memory: "                 "There is not enough real - mode memory to satisfy the request.";
            case 0x0009: return "DPMI Error 0x0009: Incorrect memory segment specified: "  "The segment value specified was not one provided by the operating system";
            case 0x8001: return "DPMI Error 0x8001: Unsupported function: "                "Returned in response to any function call which is not implemented by this host, because the requested function is either undefined or optional.";
            case 0x8002: return "DPMI Error 0x8002: Invalid state: "                       "Some object is in the wrong state for the requested operation.";
            case 0x8003: return "DPMI Error 0x8003: System integrity: "                    "The requested operation would endanger system integrity, e.g., a request to map linear addresses onto system code or data.";
            case 0x8004: return "DPMI Error 0x8004: Deadlock: "                            "Host detected a deadlock situation.";
            case 0x8005: return "DPMI Error 0x8005: Request cancelled: "                   "A pending serialization request was cancelled.";
            case 0x8010: return "DPMI Error 0x8010: Resource Unavailable: "                "The DPMI host cannot allocate internal resources to complete an operation.";
            case 0x8011: return "DPMI Error 0x8011: Descriptor unavailable: "              "Host is unable to allocate a descriptor.";
            case 0x8012: return "DPMI Error 0x8012: Linear memory unavailable: "           "Host is unable to allocate the required linear memory.";
            case 0x8013: return "DPMI Error 0x8013: Physical memory unavailable: "         "Host is unable to allocate the required physical memory.";
            case 0x8014: return "DPMI Error 0x8014: Backing store unavailable: "           "Host is unable to allocate the required backing store.";
            case 0x8015: return "DPMI Error 0x8015: Callback unavailable: "                "Host is unable to allocate the required callback address.";
            case 0x8016: return "DPMI Error 0x8016: Handle unavailable: "                  "Host is unable to allocate the required handle.";
            case 0x8017: return "DPMI Error 0x8017: Lock count exceeded: "                 "A locking operation exceeds the maximum count maintained by the host.";
            case 0x8018: return "DPMI Error 0x8018: Resource owned exclusively: "          "A request for serialization of a shared memory block could not be satisfied because it is already serialized exclusively by another client.";
            case 0x8019: return "DPMI Error 0x8019: Resource owned shared: "               "A request for exclusive serialization of a shared memory block could not be satisfied because it is already serialized shared by another client.";
            case 0x8021: return "DPMI Error 0x8021: Invalid value: "                       "A numeric or flag parameter has an invalid value.";
            case 0x8022: return "DPMI Error 0x8022: Invalid selector: "                    "A selector does not correspond to a valid descriptor.";
            case 0x8023: return "DPMI Error 0x8023: Invalid handle: "                      "A handle parameter is invalid.";
            case 0x8024: return "DPMI Error 0x8024: Invalid callback: "                    "A callback parameter is invalid.";
            case 0x8025: return "DPMI Error 0x8025: Invalid linear address: "              "A linear address range, either supplied as a parameter or implied by the call is invalid.";
            case 0x8026: return "DPMI Error 0x8026: Invalid request: "                     "The request is not supported by the underlying hardware.";
            default: ostringstream s; s << "DPMI Error 0x" << hex << ev << ": Unknown error."; return s.str();
            }
        }
    }
}
