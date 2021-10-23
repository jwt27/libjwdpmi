/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */

#pragma once

namespace jw::dpmi
{
    // Configuration flags passed to irq_handler constructor.
    enum irq_config_flags
    {
        // Always call this handler, even if the interrupt has already been acknowledged by a previous handler in the chain.
        always_call = 0b1,

        // Always chain to the default handler (usually provided by the host). Default behaviour is to chain only if the interupt has not been acknowledged.
        // Note that the default handler will always enable interrupts, which makes the no_interrupts option less effective.
        // This option effectively implies no_reentry and no_auto_eoi.
        always_chain = 0b10,

        // Don't automatically send an End Of Interrupt for this IRQ. The first call to acknowledge() will send the EOI.
        // Default behaviour is to EOI before calling any handlers, allowing interruption by lower-priority IRQs.
        no_auto_eoi = 0b100,

        // Mask the current IRQ while it is being serviced, preventing re-entry.
        no_reentry = 0b1000,

        // Mask all interrupts while this IRQ is being serviced, preventing further interruption.
        no_interrupts = 0b10000
    };
    inline constexpr irq_config_flags operator| (irq_config_flags a, auto b) { return static_cast<irq_config_flags>(static_cast<int>(a) | static_cast<int>(b)); }
    inline constexpr irq_config_flags operator|= (irq_config_flags& a, auto b) { return a = (a | b); }
}
