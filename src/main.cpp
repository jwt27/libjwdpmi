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

#include <cstring>
#include <string>
#include <deque>
#include <crt0.h>
#include <jw/dpmi/debug.h>
#include <jw/dpmi/cpu_exception.h>
#include <jw/dpmi/detail/debug.h>
#include <jw/dpmi/detail/alloc.h>
#include <jw/io/rs232.h>
#include <../jwdpmi_config.h>

using namespace jw;

int _crt0_startup_flags = 0
| config::user_crt0_startup_flags
| _CRT0_FLAG_NMI_SIGNAL
| _CRT0_DISABLE_SBRK_ADDRESS_WRAP
| _CRT0_FLAG_NONMOVE_SBRK
| _CRT0_FLAG_LOCK_MEMORY;

int jwdpmi_main(std::deque<std::string>);

inline namespace __cxxabiv1
{
    extern "C" void* __cxa_allocate_exception(std::size_t thrown_size) _GLIBCXX_NOTHROW;
}

namespace jw
{
    void print_exception(const std::exception& e, int level = 0) noexcept
    {
        std::cerr << "Level " << level << ": " << e.what() << '\n';
        try { std::rethrow_if_nested(e); }
        catch (const std::exception& e) { print_exception(e, level + 1); }
        catch (...) { std::cerr << "Level " << (level + 1) << ": Unknown exception.\n"; }
    }

    extern "C" void* irq_safe_malloc(std::size_t n)
    {
        if (dpmi::in_irq_context()) return nullptr;
        return std::malloc(n);
    }

    extern "C" void* init_malloc(std::size_t)
    {
        return nullptr;
    }

    // BLACK MAGIC HAPPENS HERE
    void patch__cxa_allocate_exception(auto* func) noexcept
    {
        auto p = reinterpret_cast<byte*>(__cxxabiv1::__cxa_allocate_exception); // take the address of __cxa_allocate_exception
        p = std::find(p, p + 0x20, 0xe8);                                       // find the first 0xe8 byte, assume this is the call to malloc.
        auto post_call = reinterpret_cast<std::uintptr_t>(p + 5);               // e8 call instruction is 5 bytes
        auto new_malloc = reinterpret_cast<std::ptrdiff_t>(func);               // take the address of new malloc
        *reinterpret_cast<std::ptrdiff_t*>(p + 1) = new_malloc - post_call;     // hotpatch __cxa_alloc to call irq_safe_malloc instead.
    }
}

int main(int argc, char** argv)
{
    patch__cxa_allocate_exception(init_malloc);
    try { throw std::array<byte, 512> { }; } catch (...) { }
    patch__cxa_allocate_exception(irq_safe_malloc);
    try { throw 0; } catch (...) { }
    _crt0_startup_flags &= ~_CRT0_FLAG_LOCK_MEMORY;

    try 
    {   
        dpmi::detail::setup_exception_throwers();
    
        std::deque<std::string> args { };   // TODO: std::string_view when it's available
        for (auto i = 0; i < argc; ++i)
        {
        #ifdef _DEBUG
            if (stricmp(argv[i], "--debug") == 0)
            {
                io::rs232_config cfg;
                cfg.set_com_port(io::com1);
                dpmi::locking_allocator<> alloc;
                dpmi::detail::setup_gdb_interface(allocate_unique<io::rs232_stream>(alloc, cfg));
            }
            else
        #else
            args.emplace_back(argv[i]);
        #endif
        }

        if (dpmi::debug())
        {
            std::cout << "Debug mode activated. Connecting to GDB..." << std::endl;
            dpmi::breakpoint();
        }
    
        return jwdpmi_main(args); 
    }
    catch (const std::exception& e) { std::cerr << "Caught exception in main()!\n"; jw::print_exception(e); }
    catch (...) { std::cerr << "Caught unknown exception in main()!\n"; }
    return -1;
}

namespace jw
{
    enum
    {
        no,
        almost,
        yes
    } new_alloc_initialized { no };
    dpmi::detail::new_allocator* new_alloc { nullptr };
    std::atomic_flag new_alloc_resize_reentry { false };
}

void* operator new(std::size_t n)
{
    if (dpmi::in_irq_context())
    {
        if (new_alloc_initialized == yes) return new_alloc->allocate(n);
        else throw std::bad_alloc { };
    }
    if (new_alloc_initialized == no)
    {
        dpmi::interrupt_mask no_interrupts_here { };
        try
        {
            new_alloc_initialized = almost;
            {
                dpmi::trap_mask dont_trap_here { };
                if (new_alloc != nullptr) delete new_alloc;
                new_alloc = nullptr;
                new_alloc = new dpmi::detail::new_allocator { };
                new_alloc_initialized = yes;
            }
        }
        catch (...)
        {
            new_alloc_initialized = no;
            throw;
        }
    }
    else if (new_alloc_initialized == yes && !new_alloc_resize_reentry.test_and_set())
    {
        dpmi::interrupt_mask no_interrupts_here { };
        dpmi::trap_mask dont_trap_here { };
        try
        {
            new_alloc_initialized = almost;
            jw::new_alloc->resize_if_necessary();
            new_alloc_initialized = yes;
        }
        catch (...)
        {
            new_alloc_initialized = no;
            new_alloc_resize_reentry.clear();
            throw;
        }
        new_alloc_resize_reentry.clear();
    }
    
    return std::malloc(n);
}

void operator delete(void* p, std::size_t)
{
    if (new_alloc_initialized == yes && new_alloc->in_pool(p))
    {
        new_alloc->deallocate(p);
        return;
    }
    std::free(p);
}

void operator delete(void* p)
{
    ::operator delete(p, 1);
}
