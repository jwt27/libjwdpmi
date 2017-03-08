/******************************* libjwdpmi **********************************
Copyright (C) 2016-2017  J.W. Jagersma

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once
#include <stdexcept>
#include <system_error>
#include <iostream>
#include <dpmi.h>

namespace jw
{
    namespace dpmi
    {
        enum dpmi_error_code : std::uint16_t
        {
            no_error = 0x0000,
            mcb_damaged = 0x0007,
            insufficient_memory = 0x0008,
            invalid_segment = 0x0009,
            unsupported_function = 0x8001,
            invalid_state = 0x8002,
            system_integritiy = 0x8003,
            deadlock = 0x8004,
            request_cancelled = 0x8005,
            resource_unavailable = 0x8010,
            descriptor_unavailable = 0x8011,
            linear_memory_unavailable = 0x8012,
            physical_memory_unavailable = 0x8013,
            backing_store_unavailable = 0x8014,
            callback_unavailable = 0x8015,
            handle_unavailable = 0x8016,
            lock_count_exceeded = 0x8017,
            resource_owned_exclusively = 0x8018,
            resource_owned_shared = 0x8019,
            invalid_value = 0x8021,
            invalid_selector = 0x8022,
            invalid_handle = 0x8023,
            invalid_callback = 0x8024,
            invalid_address = 0x8025,
            invalid_request = 0x8026
        };

        class dpmi_error_category : public std::error_category
        {
            virtual const char* name() const noexcept override { return "DPMI"; }
            virtual std::string message(int ev) const override;
        };

        class dpmi_error : public std::system_error
        {
        public:
            dpmi_error() : dpmi_error(__dpmi_error) { }
            dpmi_error(const char* message) : dpmi_error(__dpmi_error, message) { }
            dpmi_error(std::uint16_t ev) : dpmi_error(ev, "") { }
            dpmi_error(std::uint16_t ev, const char* message) : system_error(ev, dpmi_error_category(), message)
            {
                __dpmi_error = ev;
            }
        };
    }
}
