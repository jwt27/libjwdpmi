/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2018 - 2024 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <cstdint>

namespace jw::debug::detail
{
#ifndef NDEBUG
    extern bool debug_mode;
    extern volatile int current_signal;
    struct gdbstub;
#endif

    enum debug_signals : std::int32_t
    {
        packet_received = 0x1000,
        trap_unmasked,
        continued,
        thread_switched,
        thread_started,
        thread_finished,
        thread_suspended,
        all_threads_suspended,
        watchpoint_hit
    };

    enum posix_signals : std::int32_t
    {
        sighup = 1,
        sigint,
        sigquit,
        sigill,
        sigtrap,
        sigabrt,
        sigemt,
        sigfpe,
        sigkill,
        sigbus,
        sigsegv,
        sigsys,
        sigpipe,
        sigalrm,
        sigterm,
        sigstop = 17,
        sigcont = 19,
        sigusr1 = 30,
        sigusr2 = 31,
    };

#ifndef NDEBUG
    void notify_gdb_thread_event(debug::detail::debug_signals);
#else
    inline void notify_gdb_thread_event(debug::detail::debug_signals) { }
#endif
}
