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

#include <array>
#include <cstring>
#include <jw/dpmi/dpmi.h>
#include <jw/dpmi/debug.h>
#include <jw/dpmi/cpu_exception.h>
#include <jw/io/rs232.h>

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            bool gdb_interface_setup { false };

            std::array<std::unique_ptr<exception_handler>, 0x20> gdb_exception_handlers;

            std::unique_ptr<io::rs232_stream> gdb;

            template<std::uint8_t exc>
            bool gdb_handle_exception(auto* , auto* , bool )
            {
                return false;
            }

            void setup_gdb_interface(const io::rs232_config& cfg)
            {
                if (gdb_interface_setup) return;
                gdb_interface_setup = true;

                gdb = std::make_unique<io::rs232_stream>(cfg);

                gdb_exception_handlers[0x00] = std::make_unique<exception_handler>(0x00, [](auto* r, auto* f, bool t) { return gdb_handle_exception<0x00>(r, f, t); });
                gdb_exception_handlers[0x01] = std::make_unique<exception_handler>(0x01, [](auto* r, auto* f, bool t) { return gdb_handle_exception<0x01>(r, f, t); });
                gdb_exception_handlers[0x02] = std::make_unique<exception_handler>(0x02, [](auto* r, auto* f, bool t) { return gdb_handle_exception<0x02>(r, f, t); });
                gdb_exception_handlers[0x03] = std::make_unique<exception_handler>(0x03, [](auto* r, auto* f, bool t) { return gdb_handle_exception<0x03>(r, f, t); });
                gdb_exception_handlers[0x04] = std::make_unique<exception_handler>(0x04, [](auto* r, auto* f, bool t) { return gdb_handle_exception<0x04>(r, f, t); });
                gdb_exception_handlers[0x05] = std::make_unique<exception_handler>(0x05, [](auto* r, auto* f, bool t) { return gdb_handle_exception<0x05>(r, f, t); });
                gdb_exception_handlers[0x06] = std::make_unique<exception_handler>(0x06, [](auto* r, auto* f, bool t) { return gdb_handle_exception<0x06>(r, f, t); });
                gdb_exception_handlers[0x07] = std::make_unique<exception_handler>(0x07, [](auto* r, auto* f, bool t) { return gdb_handle_exception<0x07>(r, f, t); });
                gdb_exception_handlers[0x08] = std::make_unique<exception_handler>(0x08, [](auto* r, auto* f, bool t) { return gdb_handle_exception<0x08>(r, f, t); });
                gdb_exception_handlers[0x09] = std::make_unique<exception_handler>(0x09, [](auto* r, auto* f, bool t) { return gdb_handle_exception<0x09>(r, f, t); });
                gdb_exception_handlers[0x0a] = std::make_unique<exception_handler>(0x0a, [](auto* r, auto* f, bool t) { return gdb_handle_exception<0x0a>(r, f, t); });
                gdb_exception_handlers[0x0b] = std::make_unique<exception_handler>(0x0b, [](auto* r, auto* f, bool t) { return gdb_handle_exception<0x0b>(r, f, t); });
                gdb_exception_handlers[0x0c] = std::make_unique<exception_handler>(0x0c, [](auto* r, auto* f, bool t) { return gdb_handle_exception<0x0c>(r, f, t); });
                gdb_exception_handlers[0x0d] = std::make_unique<exception_handler>(0x0d, [](auto* r, auto* f, bool t) { return gdb_handle_exception<0x0d>(r, f, t); });
                gdb_exception_handlers[0x0e] = std::make_unique<exception_handler>(0x0e, [](auto* r, auto* f, bool t) { return gdb_handle_exception<0x0e>(r, f, t); });

                capabilities c { };
                if (!c.supported) return;
                if (std::strncmp(c.vendor_info.name, "HDPMI", 5) != 0) return;  // TODO: figure out if other hosts support these too
                gdb_exception_handlers[0x10] = std::make_unique<exception_handler>(0x10, [](auto* r, auto* f, bool t) { return gdb_handle_exception<0x10>(r, f, t); });
                gdb_exception_handlers[0x11] = std::make_unique<exception_handler>(0x11, [](auto* r, auto* f, bool t) { return gdb_handle_exception<0x11>(r, f, t); });
                gdb_exception_handlers[0x12] = std::make_unique<exception_handler>(0x12, [](auto* r, auto* f, bool t) { return gdb_handle_exception<0x12>(r, f, t); });
                gdb_exception_handlers[0x13] = std::make_unique<exception_handler>(0x13, [](auto* r, auto* f, bool t) { return gdb_handle_exception<0x13>(r, f, t); });
                gdb_exception_handlers[0x14] = std::make_unique<exception_handler>(0x14, [](auto* r, auto* f, bool t) { return gdb_handle_exception<0x14>(r, f, t); });
                gdb_exception_handlers[0x1e] = std::make_unique<exception_handler>(0x1e, [](auto* r, auto* f, bool t) { return gdb_handle_exception<0x1e>(r, f, t); });
            }
        }

        bool debug() { return detail::gdb_interface_setup; }
    } 
}
