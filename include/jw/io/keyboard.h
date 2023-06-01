/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2016 - 2026 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <jw/io/key.h>
#include <jw/io/detail/scancode.h>
#include <jw/io/ps2_interface.h>
#include <jw/event.h>
#include <unordered_map>
#include <memory>
#include <iostream>
#include <bitset>

namespace jw::io
{
    struct keyboard final
    {
        chain_event<bool(key_event)> key_changed;

        bool get(key k) const { return const_cast<keyboard*>(this)->keys(k); }
        bool operator[](key k) const { return get(k); }
        modifier_keys modifiers() const noexcept;

        void redirect_cin(bool echo = true, std::ostream& echo_stream = std::cout);
        void restore_cin();
        void update() { do_update(false); }
        void auto_update(bool enable);

        keyboard();
        ~keyboard();

    private:
        ps2_interface* ps2 { ps2_interface::instance().get() };
        std::array<std::bitset<0x200>, 2> state { };
        std::unique_ptr<std::streambuf> streambuf;
        static inline std::streambuf* cin { nullptr };
        static inline bool cin_redirected { false };

        void do_update(bool);

        decltype(state)::value_type::reference keys(key k)
        {
            auto i = static_cast<unsigned>(k);
            const auto ext = i >= 0x200;
            i -= ext * 0xe000;
            if (i >= 0x200)
                throw std::out_of_range { "invalid key" };
            return state[ext][i];
        }
    };
}
