/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once

#include <type_traits>
#include <functional>
#include <algorithm>
#include <memory>
#include <list>

namespace jw
{
    template <typename sig> class callback;
    template <typename R, typename ... A>
    struct callback<R(A...)>
    {
        using handler_t = std::function<R(A...)>;

        template<typename F>
        callback(F&& f) : handler_ptr(std::make_shared<handler_t>(std::forward<F>(f))) { }

        callback(const callback& c) = delete;
        callback() = delete;

        template<typename... Args>
        R operator()(Args&&... args) { return (*handler_ptr)(std::forward<Args>(args)...); }

        std::weak_ptr<handler_t> get_ptr() const { return handler_ptr; }
        operator std::weak_ptr<handler_t>() const { return get_ptr(); }

    protected:
        std::shared_ptr<handler_t> handler_ptr;
    };

    template<typename R, typename... A>
    callback(R(*)(A...)) -> callback<R(A...)>;

    template<typename F, typename Sig = typename std::__function_guide_helper<decltype(&F::operator())>::type>
    callback(F) -> callback<Sig>;

    // General event. All handlers are called, in order of subscription.
    template<typename sig> class event;
    template <typename R, typename ... A>
    struct event<R(A...)>
    {
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
            subscribers.erase(std::remove_if(v.begin(), v.end(), [&f](auto& i) { return i.expired() or i.lock() == f.get_ptr().lock(); }), v.end());
            return *this;
        }

        template<typename... Args>
        auto operator()(Args&&... args)
        {
            auto& v = subscribers;
            [[maybe_unused]] std::conditional_t<std::is_void_v<R>, int, std::vector<R>> result;
            for (auto i = v.begin(); i != v.end();)
            {
                if (not i->expired())
                {
                    auto call = [&] { return (*(i->lock()))(std::forward<Args>(args)...); };
                    if constexpr (std::is_void_v<R>) call();
                    else result.push_back(call());
                    ++i;
                }
                else i = v.erase(i);
            }
            if constexpr (not std::is_void_v<R>) return result;
        }

    private:
        std::list<std::weak_ptr<event_handler>> subscribers;
    };

    // Chaining event. Last subscribed handler is called first.
    // Each event handler returns a boolean value. The chain ends when a callback returns true.
    template<typename sig> class chain_event;
    template <typename R, typename ... A>
    struct chain_event<R(A...)>
    {
        static_assert(std::is_same_v<R, bool>);
        using callback_t = callback<R(A...)>;
        using event_handler = typename callback_t::handler_t;

        chain_event& operator+=(callback_t& f)
        {
            subscribers.push_front(f.get_ptr());
            return *this;
        }

        chain_event& operator-=(callback_t& f)
        {
            auto& v = subscribers;
            subscribers.erase(std::remove_if(v.begin(), v.end(), [&f](auto& i) { return i.expired() or i.lock() == f.get_ptr().lock(); }), v.end());
            return *this;
        }

        template<typename... Args>
        bool operator()(Args&&... args)
        {
            auto& v = subscribers;
            for (auto i = v.begin(); i != v.end();)
            {
                if (not i->expired())
                {
                    if ((*(i->lock()))(std::forward<Args>(args)...)) return true;
                    ++i;
                }
                else i = v.erase(i);
            }
            return false;
        }

    private:
        std::list<std::weak_ptr<event_handler>> subscribers;
    };
}

