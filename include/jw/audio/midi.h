/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <variant>
#include <vector>
#include <iostream>
#include <optional>
#include <jw/common.h>
#include <jw/split_stdint.h>
#include <jw/chrono/chrono.h>
#include <../jwdpmi_config.h>

namespace jw
{
    using split_uint14_t = split_int<unsigned, 14>;
}

namespace jw::audio
{
    struct midi
    {
        using clock = jw::config::midi_clock;

        // tag types
        struct channel_message { byte channel; };
        struct system_message { };
        struct system_common_message    : system_message { };
        struct system_realtime_message  : system_message { };

        // channel message types
        struct note_event           : channel_message { bool on; byte key, velocity; };
        struct key_pressure         : channel_message { byte key, value; };
        struct channel_pressure     : channel_message { byte value; };
        struct control_change       : channel_message { byte controller, value; };
        struct long_control_change  : channel_message { byte controller; split_uint14_t value; };   // never received
        struct program_change       : channel_message { byte value; };
        struct pitch_change         : channel_message { split_uint14_t value; };
        struct rpn_change           : channel_message { split_uint14_t parameter, value; };         // never received
        struct nrpn_change          : channel_message { split_uint14_t parameter, value; };         // never received

        // system message types
        struct sysex                : system_common_message { std::vector<byte> data; };
        struct mtc_quarter_frame    : system_common_message { byte data; };                         // TODO
        struct song_position        : system_common_message { split_uint14_t value; };
        struct song_select          : system_common_message { byte value; };
        struct tune_request         : system_common_message { };
        struct clock_tick           : system_realtime_message { };
        struct clock_start          : system_realtime_message { };
        struct clock_continue       : system_realtime_message { };
        struct clock_stop           : system_realtime_message { };
        struct active_sense         : system_realtime_message { };
        struct reset                : system_realtime_message { };

        std::variant<
            note_event,
            key_pressure,
            channel_pressure,
            control_change,
            long_control_change,
            program_change,
            pitch_change,
            rpn_change,
            nrpn_change,
            sysex,
            mtc_quarter_frame,
            song_position,
            song_select,
            tune_request,
            clock_tick,
            clock_start,
            clock_continue,
            clock_stop,
            active_sense,
            reset> msg;
        typename clock::time_point time;

        template<typename T, std::enable_if_t<not std::is_base_of_v<std::istream, T>, int> = 0> constexpr midi(T&& m)
            : midi { std::forward<T>(m), clock::time_point::min() } { }

        template<typename T, std::enable_if_t<std::is_base_of_v<std::istream, T>, int> = 0> constexpr midi(T& in)
            : midi { extract(in) } { }

        template<typename T> constexpr midi(T&& m, clock::time_point t)
            : msg(std::forward<T>(m)), time(t) { }

        constexpr midi() noexcept = default;
        midi(const midi&) noexcept = default;
        midi(midi&&) noexcept = default;
        midi& operator=(const midi&) noexcept = default;
        midi& operator=(midi&&) noexcept = default;

        bool is_channel_message() const
        {
            return std::visit([](auto&& m) { return std::is_base_of_v<channel_message, std::decay_t<decltype(m)>>; }, msg);
        }

        bool is_system_message() const
        {
            return std::visit([](auto&& m) { return std::is_base_of_v<system_message, std::decay_t<decltype(m)>>; }, msg);
        }

        bool is_realtime_message() const
        {
            return std::visit([](auto&& m) { return std::is_base_of_v<system_realtime_message, std::decay_t<decltype(m)>>; }, msg);
        }

        auto channel() const
        {
            return std::visit([](auto&& m) -> std::optional<std::uint32_t>
            {
                if constexpr (std::is_base_of_v<channel_message, std::decay_t<decltype(m)>>)
                    return { m.channel };
                return { };
            }, msg);
        }

        void emit(std::ostream& out) const;
        static midi extract(std::istream& in);
    };

    inline std::ostream& operator<<(std::ostream& out, const midi& in) { in.emit(out); return out; }
    inline std::istream& operator>>(std::istream& in, midi& out) { out = midi::extract(in); return in; }
}
