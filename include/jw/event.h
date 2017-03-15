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

#pragma once

#include <functional>
#include <algorithm>
#include <memory>
#include <vector>

namespace jw
{
    template <typename sig> class callback;
    template <typename R, typename ... A>
    class callback<R(A...)>
    {
    public:
        using handler_t = std::function<R(A...)>;

        template<typename F>
        callback(F&& f) : handler_ptr(std::make_shared<handler_t>(std::forward<F>(f))) { }

        callback(const callback& c) = delete;
        callback() = delete;

        R operator()(A... args) { return (*handler_ptr)(std::forward<A>(args)...); }

        std::weak_ptr<handler_t> get_ptr() const { return handler_ptr; }
        operator std::weak_ptr<handler_t>() const { return get_ptr(); }

    protected:
        std::shared_ptr<handler_t> handler_ptr;
    };

    template<typename sig> class event;
    template <typename R, typename ... A>
    class event<R(A...)>
    {
    public:
        using callback_t = callback<R(A...)>;
        using event_handler = typename callback_t::handler_t;

        event& operator+=(callback_t& f)
        {
            subscribers.push_back(f.get_ptr());
            return *this;
        }

        event& operator-=(callback_t& f)
        {                         
            auto& v = subscribers;
            subscribers.erase(std::remove_if(v.begin(), v.end(), [&f](auto& i) { return i.expired() || i.lock() == f.get_ptr().lock(); }), v.end());
            return *this;
        }

        auto operator()(A... args)
        {
            auto& v = subscribers;
            subscribers.erase(std::remove_if(v.begin(), v.end(), [](auto& i) { return i.expired(); }), v.end());
            return call(std::is_void<R> { }, std::forward<A>(args)...);
        }

    protected:
        void call(std::true_type, A... args)
        {
            for (auto i : subscribers) (*i.lock())(std::forward<A>(args)...);
        }

        std::vector<R> call(std::false_type, A... args)
        {
            std::vector<R> result;
            for (auto i : subscribers) result.push_back((*i.lock())(std::forward<A>(args)...));
            return result;
        }

    private:
        std::vector<std::weak_ptr<event_handler>> subscribers;
    };
}

