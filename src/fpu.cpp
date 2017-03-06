#include <jw/dpmi/fpu.h>
#include <jw/dpmi/cpu_exception.h>

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            bool test_cr0_access()
            {
                static bool known = false;
                static volatile bool cr0_allowed = true;
                if (!known)
                {
                    dpmi::exception_handler exc { 0x0d, [&](auto* frame, bool)
                    {
                        cr0_allowed = false;
                        frame->fault_address.offset += 3;
                        return true;
                    } };

                    asm("mov eax, cr0;" // both instructions are 3 bytes
                        "mov cr0, eax;"
                        :::"eax");

                    known = true;
                }
                return cr0_allowed;
            }

            bool test_cr4_access()
            {
                static bool known = false;
                static volatile bool cr4_allowed = true;
                if (!known)
                {
                    dpmi::exception_handler exc { 0x0d, [&](auto* frame, bool)
                    {
                        cr4_allowed = false;
                        frame->fault_address.offset += 3;
                        return true;
                    } };

                    asm("mov eax, cr4;"
                        "mov cr4, eax;"
                        :::"eax");

                    known = true;
                }
                return cr4_allowed;
            }
            
            fpu_context_switcher_t fpu_context_switcher;
            std::unique_ptr<exception_handler> exc07_handler;
            //fpu_context_switcher::initializer fpu_context_switcher::init;
            //locked_pool_allocator<> fpu_context_switcher::alloc { config::interrupt_fpu_context_pool };
            //std::unique_ptr<std::unordered_map<std::uint32_t, fpu_context, std::hash<std::uint32_t>, std::equal_to<std::uint32_t>, locked_pool_allocator<>>> fpu_context_switcher::contexts { alloc };
            //fpu_context fpu_context_switcher::irq_context;
            //bool fpu_context_switcher::lazy_switching { false };
            //volatile std::uint32_t fpu_context_switcher::count { 0 };

            fpu_context_switcher_t::fpu_context_switcher_t()
            {
            #ifdef __SSE__
                std::cerr << "a" << std::endl;
                if (test_cr4_access())
                {
                    asm("mov eax, cr4;"
                        "or eax, 0x600;" // enable SSE and SSE exceptions
                        "mov cr4,eax;"
                        :::"eax");
                }
            #endif

                cr0_t cr0 { };
                cr0.native_exceptions = true;
                cr0.set();

                std::cerr << "b" << std::endl;

                std::cerr << "c" << std::endl;

                auto current_context = std::make_unique<fpu_context>();
                std::clog << "alignof=" << std::hex << alignof(fpu_context) << " ptr=" << (int)&current_context << '\n';
                std::clog << "alignof=" << std::hex << alignof(*current_context) << " ptr=" << (int)&irq_context << '\n';
                //std::clog << alignof(std::max_align_t) << '\n';
                //std::cerr<< " data" <<    << std::endl;

                //asm("int 3;");
                current_context->save();
                asm("fninit;"
                    "sub esp, 4;"
                    "fnstcw [esp];"
                    "or word ptr [esp], 0x00BF;"   // mask all exceptions
                    "fldcw [esp];"
            #ifdef __SSE__
                    "stmxcsr [esp];"
                    "or dword ptr [esp], 0x00001F80;"
                    "ldmxcsr [esp];"
            #endif
                    "add esp, 4;");
                irq_context.save();
                //current_context->restore();
                std::cerr << "d" << std::endl;

                if (!test_cr0_access() || cr0.fpu_emulation) return;
                std::cerr << "e" << std::endl;
                exc07_handler = std::make_unique<exception_handler>(0x07, [&](exception_frame*, bool)
                {
                    std::cerr << "handling exc 07" << std::endl;
                    auto i = static_cast<std::uint32_t>(count);
                    if (contexts.count(i)) contexts[i].restore();
                    else save();
                    return true;
                });
                std::cerr << "f" << std::endl;
                lazy_switching = true;
            }
        }
    }
}