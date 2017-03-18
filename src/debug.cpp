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
#include <sstream>
#include <iomanip>
#include <jw/dpmi/dpmi.h>
#include <jw/dpmi/debug.h>
#include <jw/dpmi/cpu_exception.h>
#include <jw/io/rs232.h>
#include <jw/alloc.h>

/* Register numbers from gdb-7.12.1/gdb/i386-tdep.h
enum i386_regnum
{
    I386_EAX_REGNUM,
    I386_ECX_REGNUM,
    I386_EDX_REGNUM,
    I386_EBX_REGNUM,
    I386_ESP_REGNUM,
    I386_EBP_REGNUM,
    I386_ESI_REGNUM,
    I386_EDI_REGNUM,
    I386_EIP_REGNUM,
    I386_EFLAGS_REGNUM,
    I386_CS_REGNUM,
    I386_SS_REGNUM,
    I386_DS_REGNUM,
    I386_ES_REGNUM,
    I386_FS_REGNUM,
    I386_GS_REGNUM,
    I386_ST0_REGNUM,
    I386_MXCSR_REGNUM = 40,
    I386_YMM0H_REGNUM,
    I386_YMM7H_REGNUM = I386_YMM0H_REGNUM + 7,
    I386_BND0R_REGNUM,
    I386_BND3R_REGNUM = I386_BND0R_REGNUM + 3,
    I386_BNDCFGU_REGNUM,
    I386_BNDSTATUS_REGNUM,
    I386_K0_REGNUM,
    I386_K7_REGNUM = I386_K0_REGNUM + 7,
    I386_ZMM0H_REGNUM,
    I386_ZMM7H_REGNUM = I386_ZMM0H_REGNUM + 7
};*/

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            bool gdb_interface_setup { false };
        }

        namespace gdb
        {
            locked_pool_allocator<> alloc { 1_MB };
            std::deque<std::string, locked_pool_allocator<>> sent { alloc };

            std::array<std::unique_ptr<exception_handler>, 0x20> exception_handlers;
            auto gdb { init_unique<std::iostream>(alloc) };

            std::uint32_t checksum(const std::string& s)
            {
                std::uint8_t r { };
                for (auto c : s) r += c;
                return r;
            }       
            
            void send_packet(const std::string& output)
            {
                const auto sum = checksum(output);
                *gdb << '$' << output << '#' << std::setfill('0') << std::setw(2) << sum << std::flush;
                sent.push_back(output);
            }

            std::string recv_packet()
            {
            retry:
                switch (gdb->get())
                {
                case '-': send_packet(sent.front());
                case '+': sent.pop_front(); 
                default: goto retry;
                case '#': break;
                }
                std::string input;
                std::getline(*gdb, input, '#');
                std::string sum;
                sum += gdb->get();
                sum += gdb->get();
                if (std::strtoul(sum.c_str(), nullptr, 0x10) == checksum(input)) *gdb << '+';
                else
                {
                    *gdb << '-';
                    goto retry;
                }
                return input;
            }

            template<std::uint32_t exc>
            bool handle_exception(auto* , auto* , bool )
            {
                std::stringstream str { };
                str << "S " << std::setw(2) << exc;
                send_packet(str.str());
                return false;
            }

            void setup(const io::rs232_config& cfg)
            {
                if (jw::dpmi::detail::gdb_interface_setup) return;
                jw::dpmi::detail::gdb_interface_setup = true;

                gdb = allocate_unique<io::rs232_stream>(alloc, cfg);

                exception_handlers[0x00] = std::make_unique<exception_handler>(0x00, [](auto* r, auto* f, bool t) { return handle_exception<0x00>(r, f, t); });
                exception_handlers[0x01] = std::make_unique<exception_handler>(0x01, [](auto* r, auto* f, bool t) { return handle_exception<0x01>(r, f, t); });
                exception_handlers[0x02] = std::make_unique<exception_handler>(0x02, [](auto* r, auto* f, bool t) { return handle_exception<0x02>(r, f, t); });
                exception_handlers[0x03] = std::make_unique<exception_handler>(0x03, [](auto* r, auto* f, bool t) { return handle_exception<0x03>(r, f, t); });
                exception_handlers[0x04] = std::make_unique<exception_handler>(0x04, [](auto* r, auto* f, bool t) { return handle_exception<0x04>(r, f, t); });
                exception_handlers[0x05] = std::make_unique<exception_handler>(0x05, [](auto* r, auto* f, bool t) { return handle_exception<0x05>(r, f, t); });
                exception_handlers[0x06] = std::make_unique<exception_handler>(0x06, [](auto* r, auto* f, bool t) { return handle_exception<0x06>(r, f, t); });
                exception_handlers[0x07] = std::make_unique<exception_handler>(0x07, [](auto* r, auto* f, bool t) { return handle_exception<0x07>(r, f, t); });
                exception_handlers[0x08] = std::make_unique<exception_handler>(0x08, [](auto* r, auto* f, bool t) { return handle_exception<0x08>(r, f, t); });
                exception_handlers[0x09] = std::make_unique<exception_handler>(0x09, [](auto* r, auto* f, bool t) { return handle_exception<0x09>(r, f, t); });
                exception_handlers[0x0a] = std::make_unique<exception_handler>(0x0a, [](auto* r, auto* f, bool t) { return handle_exception<0x0a>(r, f, t); });
                exception_handlers[0x0b] = std::make_unique<exception_handler>(0x0b, [](auto* r, auto* f, bool t) { return handle_exception<0x0b>(r, f, t); });
                exception_handlers[0x0c] = std::make_unique<exception_handler>(0x0c, [](auto* r, auto* f, bool t) { return handle_exception<0x0c>(r, f, t); });
                exception_handlers[0x0d] = std::make_unique<exception_handler>(0x0d, [](auto* r, auto* f, bool t) { return handle_exception<0x0d>(r, f, t); });
                exception_handlers[0x0e] = std::make_unique<exception_handler>(0x0e, [](auto* r, auto* f, bool t) { return handle_exception<0x0e>(r, f, t); });

                capabilities c { };
                if (!c.supported) return;
                if (std::strncmp(c.vendor_info.name, "HDPMI", 5) != 0) return;  // TODO: figure out if other hosts support these too
                //exception_handlers[0x10] = std::make_unique<exception_handler>(0x10, [](auto* r, auto* f, bool t) { return handle_exception<0x10>(r, f, t); });
                exception_handlers[0x11] = std::make_unique<exception_handler>(0x11, [](auto* r, auto* f, bool t) { return handle_exception<0x11>(r, f, t); });
                exception_handlers[0x12] = std::make_unique<exception_handler>(0x12, [](auto* r, auto* f, bool t) { return handle_exception<0x12>(r, f, t); });
                exception_handlers[0x13] = std::make_unique<exception_handler>(0x13, [](auto* r, auto* f, bool t) { return handle_exception<0x13>(r, f, t); });
                exception_handlers[0x14] = std::make_unique<exception_handler>(0x14, [](auto* r, auto* f, bool t) { return handle_exception<0x14>(r, f, t); });
                exception_handlers[0x1e] = std::make_unique<exception_handler>(0x1e, [](auto* r, auto* f, bool t) { return handle_exception<0x1e>(r, f, t); });
            }
        }

        namespace detail
        {
            void setup_gdb_interface(const io::rs232_config& cfg) { jw::dpmi::gdb::setup(cfg); }
        }

        bool debug() { return detail::gdb_interface_setup; }
    } 
}
