/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */

#pragma once

namespace jw::thread::detail
{
    template <typename M>
    struct timed_mutex_adapter : public M
    {
        template <class Rep, class Period>
        bool try_lock_for(const std::chrono::duration<Rep, Period>& rel_time)
        {
            return not yield_while_for([this] { return not this->try_lock(); }, rel_time);
        }

        template <class Clock, class Duration>
        bool try_lock_until(const std::chrono::time_point<Clock, Duration>& abs_time)
        {
            return not yield_while_until([this] { return not this->try_lock(); }, abs_time);
        }
    };
}
