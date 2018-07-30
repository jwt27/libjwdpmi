/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */

#include <cstdint>

namespace jw
{
    namespace debug
    {
        namespace detail
        {
            enum debug_signals : std::int32_t
            {
                packet_received = 0x1000,
                trap_unmasked,
                continued,
                thread_switched,
                thread_started,
                thread_finished,
                thread_suspended,
                all_threads_suspended
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
        }
    }
}
