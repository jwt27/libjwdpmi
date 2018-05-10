/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <variant>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <jw/common.h>
#include <jw/split_stdint.h>
#include <jw/thread/thread.h>
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
        struct system_common_message : public system_message { };
        struct system_realtime_message : public system_message { };

        // channel message types
        struct note_event : public channel_message { bool on; byte key, velocity; };
        struct key_pressure : public channel_message { byte key, value; };
        struct channel_pressure : public channel_message { byte value; };
        struct control_change : public channel_message { byte controller, value; };
        struct long_control_change : public channel_message { byte controller; split_uint14_t value; }; // never received
        struct program_change : public channel_message { byte value; };
        struct pitch_change : public channel_message { split_uint14_t value; };
        struct rpn_change : public channel_message { split_uint14_t parameter, value; }; // never received
        struct nrpn_change : public channel_message { split_uint14_t parameter, value; }; // never received

        // system message types
        struct sysex : public system_common_message { std::vector<byte> data; };
        struct mtc_quarter_frame : public system_common_message { byte data; };    // TODO
        struct song_position : public system_common_message { split_uint14_t value; };
        struct song_select : public system_common_message { byte value; };
        struct tune_request : public system_common_message { };
        struct clock_tick : public system_realtime_message { };
        struct clock_start : public system_realtime_message { };
        struct clock_continue : public system_realtime_message { };
        struct clock_stop : public system_realtime_message { };
        struct active_sense : public system_realtime_message { };
        struct reset : public system_realtime_message { };

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

        midi(std::istream& in) { in >> *this; }
        template<typename T> constexpr midi(T&& m, typename clock::time_point t = clock::time_point::min()) : msg(std::forward<T>(m)), time(t) { }

    protected:
        inline static std::unordered_map<std::istream*, byte> last_status_rx { };
        inline static std::unordered_map<std::ostream*, byte> last_status_tx { };

        struct stream_writer
        {
            std::ostream& out;

            void put_status(byte a)
            {
                if (last_status_tx[&out] != a) out.put(a);
                last_status_tx[&out] = a;
            }

            void clear_status() { last_status_tx[&out] = 0; }

            void operator()(const note_event& msg)
            {
                if (not msg.on and last_status_tx[&out] == (0x90 | (msg.channel & 0x0f)))
                {
                    put_status(0x90 | (msg.channel & 0x0f));
                    out.put(msg.key);
                    out.put(0x00);
                }
                else
                {
                    put_status((msg.on ? 0x90 : 0x80) | (msg.channel & 0x0f));
                    out.put(msg.key);
                    out.put(msg.velocity);
                }
            }
            void operator()(const key_pressure& msg)        { put_status(0xa0 | (msg.channel & 0x0f)); out.put(msg.key); out.put(msg.value); }
            void operator()(const control_change& msg)      { put_status(0xb0 | (msg.channel & 0x0f)); out.put(msg.controller); out.put(msg.value); }
            void operator()(const program_change& msg)      { put_status(0xc0 | (msg.channel & 0x0f)); out.put(msg.value); }
            void operator()(const channel_pressure& msg)    { put_status(0xd0 | (msg.channel & 0x0f)); out.put(msg.value); }
            void operator()(const pitch_change& msg)        { put_status(0xe0 | (msg.channel & 0x0f)); out.put(msg.value.lo); out.put(msg.value.hi); }

            void operator()(const sysex& msg)               { clear_status(); out.put(0xf0); out.write(reinterpret_cast<const char*>(msg.data.data()), msg.data.size()); out.put(0xf7); }
            void operator()(const mtc_quarter_frame& msg)   { clear_status(); out.put(0xf1); out.put(msg.data); }
            void operator()(const song_position& msg)       { clear_status(); out.put(0xf2); out.put(msg.value.lo); out.put(msg.value.hi); }
            void operator()(const song_select& msg)         { clear_status(); out.put(0xf3); out.put(msg.value); }

            void operator()(const tune_request&)    { out.put(0xf6); }
            void operator()(const clock_tick&)      { out.put(0xf8); }
            void operator()(const clock_start&)     { out.put(0xfa); }
            void operator()(const clock_continue&)  { out.put(0xfb); }
            void operator()(const clock_stop&)      { out.put(0xfc); }
            void operator()(const active_sense&)    { out.put(0xfe); }
            void operator()(const reset&)           { out.put(0xff); }

            void operator()(const long_control_change& msg)
            {
                out << midi { control_change { { msg.channel }, msg.controller, static_cast<byte>(msg.value.hi) } };
                out << midi { control_change { { msg.channel }, static_cast<byte>(msg.controller + 32), static_cast<byte>(msg.value.lo) } };
            }

            void operator()(const rpn_change& msg)
            {
                out << midi { control_change { { msg.channel }, 0x65, static_cast<byte>(msg.parameter.hi) } };
                out << midi { control_change { { msg.channel }, 0x64, static_cast<byte>(msg.parameter.lo) } };
                out << midi { long_control_change { { msg.channel }, 0x06, msg.value } };
            }

            void operator()(const nrpn_change& msg)
            {
                out << midi { control_change { { msg.channel }, 0x63, static_cast<byte>(msg.parameter.hi) } };
                out << midi { control_change { { msg.channel }, 0x62, static_cast<byte>(msg.parameter.lo) } };
                out << midi { long_control_change { { msg.channel }, 0x06, msg.value } };
            }
        };

    public:
        friend std::ostream& operator<<(std::ostream& out, const midi& in)
        {
            std::visit(stream_writer { out }, in.msg);
            return out;
        }

        friend std::istream& operator>>(std::istream& in, midi& out)
        {
            byte a { last_status_rx[&in] };
            auto get = [&in] { return static_cast<byte>(in.get()); };
            if ((in.peek() & 0x80) == 0 and a == 0) do { a = get(); } while ((a & 0x80) == 0);
            out.time = clock::now();
            if ((a & 0xf0) != 0xf0)   // channel message
            {
                last_status_rx[&in] = a;
                byte ch = a & 0x0f;
                switch (a & 0xf0)
                {
                case 0x80: out.msg = note_event { { ch }, false, get(), get() }; break;
                case 0x90:
                {
                    auto key = get();
                    auto vel = get();
                    if (vel == 0) out.msg = note_event { { ch }, false, key, vel };
                    else out.msg = note_event { { ch }, true, key, vel };
                    break;
                }
                case 0xa0: out.msg = key_pressure { { ch }, get(), get() }; break;
                case 0xb0: out.msg = control_change { { ch }, get(), get() }; break;
                case 0xc0: out.msg = program_change { { ch }, get() }; break;
                case 0xd0: out.msg = channel_pressure { { ch }, get() }; break;
                case 0xe0: out.msg = pitch_change { { ch }, { get(), get() } }; break;
                }
            }
            else                    // system message
            {
                if (a < 0xf8) last_status_rx[&in] = 0;
                switch (a)
                {
                case 0xf0:
                {
                    out.msg = sysex { };
                    auto& data = std::get<sysex>(out.msg).data;
                    std::vector<char> extra { };
                    while (true)
                    {
                        a = get();
                        if (a == 0xf7) break;       // end of sysex
                        if ((a & 0x80) == 0) data.push_back(a);
                        else extra.push_back(a);    // single-byte realtime messages may be mixed in
                    }
                    for (auto&& i : extra) in.putback(i);
                    break;
                }
                case 0xf1: out.msg = mtc_quarter_frame { { }, get() }; break;
                case 0xf2: out.msg = song_position { { }, { get(), get() } }; break;
                case 0xf3: out.msg = song_select { { }, get() }; break;
                case 0xf6: out.msg = tune_request { }; break;
                case 0xf8: out.msg = clock_tick { }; break;
                case 0xfa: out.msg = clock_start { }; break;
                case 0xfb: out.msg = clock_continue { }; break;
                case 0xfc: out.msg = clock_stop { }; break;
                case 0xfe: out.msg = active_sense { }; break;
                case 0xff: out.msg = reset { }; break;
                }
            }
            return in;
        }
    };
}
