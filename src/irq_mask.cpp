#include <jw/dpmi/irq_mask.h>

using namespace std;
namespace jw
{
    namespace dpmi
    {
        volatile int interrupt_mask::count { 0 };
        bool interrupt_mask::initial_state;

        std::array<irq_mask::mask_counter, 16> irq_mask::map { };
        //const io::io_port<byte> irq_mask::pic0_data_port { 0x21 };
        //const io::io_port<byte> irq_mask::pic1_data_port { 0xA1 };

        //int nmi_mask::count { 0 };
    }
}
