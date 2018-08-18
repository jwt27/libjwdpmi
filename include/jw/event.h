/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once

#include <functional>
#include <algorithm>
#include <memory>
#include <list>

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

        template<typename... Args>
        auto operator()(Args&&... args)
        {
            auto& v = subscribers;
            subscribers.erase(std::remove_if(v.begin(), v.end(), [](auto& i) { return i.expired(); }), v.end());
            return call(std::is_void<R> { }, std::forward<Args>(args)...);
        }

    protected:
        template<typename... Args>
        void call(std::true_type, Args&&... args)
        {
            for (auto i : subscribers) (*i.lock())(std::forward<Args>(args)...);
        }

        template<typename... Args>
        std::vector<R> call(std::false_type, Args&&... args)
        {
            std::vector<R> result;
            for (auto i : subscribers) result.push_back((*i.lock())(std::forward<Args>(args)...));
            return result;
        }

    private:
        std::list<std::weak_ptr<event_handler>> subscribers;
    };
}

