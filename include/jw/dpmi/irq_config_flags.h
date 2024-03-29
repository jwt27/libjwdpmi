/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2023 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */

#pragma once

namespace jw::dpmi
{
    // Configuration flags passed to irq_handler constructor.
    enum irq_config_flags : std::uint8_t
    {
        // Always chain to the real-mode handler. Default behaviour is to
        // chain only if the interupt has not been acknowledged.  Do make sure
        // there is actually a real-mode handler installed, otherwise the BIOS
        // will mask the IRQ line.
        always_chain = 0b1,

        // Don't automatically send an End Of Interrupt for this IRQ. The
        // first call to acknowledge() will send the EOI.  Default behaviour
        // is to EOI before calling any handlers, allowing interruption by
        // lower-priority IRQs.  Most devices will need this flag.
        no_auto_eoi = 0b10,

        // Send an EOI only after all IRQ handlers have been called.  This is
        // effectively similar to no_reentry, but lower priority IRQs will
        // also be inhibited.
        late_eoi = 0b100,

        // Mask the current IRQ while it is being serviced, preventing
        // re-entry.
        no_reentry = 0b1000,

        // Mask all interrupts while this IRQ is being serviced, preventing
        // further interruption from both lower and higher priority IRQs.
        no_interrupts = 0b10000,

        // Specifies that this IRQ handler may only be invoked if no other
        // handler acknowledges the interrupt.  This is used for devices which
        // do not provide a status flag to identify themselves as the
        // interrupt source.
        // Only one device per IRQ line may be registered with this flag.
        fallback_handler = 0b100000
    };
    inline constexpr irq_config_flags operator| (irq_config_flags a, auto b) { return static_cast<irq_config_flags>(static_cast<int>(a) | static_cast<int>(b)); }
    inline constexpr irq_config_flags operator|= (irq_config_flags& a, auto b) { return a = (a | b); }
}
