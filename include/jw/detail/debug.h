/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2018 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <cstdint>
#include <csignal>

namespace jw::detail
{
    struct thread;
}

namespace jw::debug::detail
{
#ifndef NDEBUG
    extern bool debug_mode;
    extern int current_signal;
    struct gdbstub;
#endif

    enum debug_signals : std::int32_t
    {
        packet_received = SIGMAX,
        continued,
        thread_started,
        thread_finished,
        print_message
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
        sigmax
    };

#ifndef NDEBUG
    void create_thread(jw::detail::thread*);
    void destroy_thread(jw::detail::thread*);
#else
    inline void create_thread(jw::detail::thread*) { }
    inline void destroy_thread(jw::detail::thread*) { }
#endif

    struct stacktrace_base
    {
    protected:
        static std::size_t make(std::uintptr_t*, std::size_t, int);
        static void print(FILE*, std::span<const std::uintptr_t>);
    };
}
