/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <variant>
#include <vector>
#include <deque>
#include <iostream>
#include <list>
#include <mutex>
#include <optional>
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

        constexpr midi() noexcept { }
        midi(std::istream& in) { in >> *this; }
        template<typename T> constexpr midi(T&& m, typename clock::time_point t = clock::time_point::min()) : msg(std::forward<T>(m)), time(t) { }

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

    protected:
        struct istream_info
        {
            std::mutex mutex { };
            std::deque<byte> pending_msg { };
            clock::time_point pending_msg_time;
            byte last_status { 0 };
        };
        struct ostream_info
        {
            std::mutex mutex { };
            byte last_status { 0 };
        };
        inline static std::list<istream_info> istream_list { };
        inline static std::list<ostream_info> ostream_list { };

        template <typename S, typename L>
        static void* get_pword(int i, S& stream, L& list)
        {
            void*& p = stream.pword(i);
            if (p == nullptr) [[unlikely]]
                p = &list.emplace_back();
            return p;
        }

        static istream_info& rx_state(std::istream& stream)
        {
            static const int i = std::ios_base::xalloc();
            return *static_cast<istream_info*>(get_pword(i, stream, istream_list));
        }

        static ostream_info& tx_state(std::ostream& stream)
        {
            static const int i = std::ios_base::xalloc();
            return *static_cast<ostream_info*>(get_pword(i, stream, ostream_list));
        }

        struct stream_writer
        {
            std::streambuf& out;
            ostream_info& tx;

            void put_status(byte a)
            {
                if (tx.last_status != a) out.sputc(a);
                tx.last_status = a;
            }

            void clear_status() { tx.last_status = 0; }

            void operator()(const note_event& msg)
            {
                if (not msg.on and tx.last_status == (0x90 | (msg.channel & 0x0f)))
                {
                    put_status(0x90 | (msg.channel & 0x0f));
                    out.sputc(msg.key);
                    out.sputc(0x00);
                }
                else
                {
                    put_status((msg.on ? 0x90 : 0x80) | (msg.channel & 0x0f));
                    out.sputc(msg.key);
                    out.sputc(msg.velocity);
                }
            }
            void operator()(const key_pressure& msg)        { put_status(0xa0 | (msg.channel & 0x0f)); out.sputc(msg.key); out.sputc(msg.value); }
            void operator()(const control_change& msg)      { put_status(0xb0 | (msg.channel & 0x0f)); out.sputc(msg.controller); out.sputc(msg.value); }
            void operator()(const program_change& msg)      { put_status(0xc0 | (msg.channel & 0x0f)); out.sputc(msg.value); }
            void operator()(const channel_pressure& msg)    { put_status(0xd0 | (msg.channel & 0x0f)); out.sputc(msg.value); }
            void operator()(const pitch_change& msg)        { put_status(0xe0 | (msg.channel & 0x0f)); out.sputc(msg.value.lo); out.sputc(msg.value.hi); }

            void operator()(const sysex& msg)               { clear_status(); out.sputc(0xf0); out.sputn(reinterpret_cast<const char*>(msg.data.data()), msg.data.size()); out.sputc(0xf7); }
            void operator()(const mtc_quarter_frame& msg)   { clear_status(); out.sputc(0xf1); out.sputc(msg.data); }
            void operator()(const song_position& msg)       { clear_status(); out.sputc(0xf2); out.sputc(msg.value.lo); out.sputc(msg.value.hi); }
            void operator()(const song_select& msg)         { clear_status(); out.sputc(0xf3); out.sputc(msg.value); }

            void operator()(const tune_request&)    { out.sputc(0xf6); }
            void operator()(const clock_tick&)      { out.sputc(0xf8); }
            void operator()(const clock_start&)     { out.sputc(0xfa); }
            void operator()(const clock_continue&)  { out.sputc(0xfb); }
            void operator()(const clock_stop&)      { out.sputc(0xfc); }
            void operator()(const active_sense&)    { out.sputc(0xfe); }
            void operator()(const reset&)           { out.sputc(0xff); }

            void operator()(const long_control_change& msg)
            {
                (*this)(control_change { { msg.channel }, msg.controller, static_cast<byte>(msg.value.hi) });
                (*this)(control_change { { msg.channel }, static_cast<byte>(msg.controller + 0x20), static_cast<byte>(msg.value.lo) });
            }

            void operator()(const rpn_change& msg)
            {
                (*this)(control_change { { msg.channel }, 0x65, static_cast<byte>(msg.parameter.hi) });
                (*this)(control_change { { msg.channel }, 0x64, static_cast<byte>(msg.parameter.lo) });
                (*this)(long_control_change { { msg.channel }, 0x06, msg.value });
            }

            void operator()(const nrpn_change& msg)
            {
                (*this)(control_change { { msg.channel }, 0x63, static_cast<byte>(msg.parameter.hi) });
                (*this)(control_change { { msg.channel }, 0x62, static_cast<byte>(msg.parameter.lo) });
                (*this)(long_control_change { { msg.channel }, 0x06, msg.value });
            }
        };

        template <typename S>
        static void iostream_exception(S& stream)
        {
            stream.setstate(std::ios::badbit);
            if (stream.exceptions() & stream.rdstate()) throw;
        };

    public:
        friend std::ostream& operator<<(std::ostream& out, const midi& in)
        {
            auto& tx { tx_state(out) };
            std::unique_lock<std::mutex> lock { tx.mutex, std::defer_lock };
            if (not in.is_realtime_message()) lock.lock();
            std::ostream::sentry sentry { out };
            try
            {
                if (sentry) std::visit(stream_writer { *out.rdbuf(), tx }, in.msg);
            }
            catch (const terminate_exception&)  { iostream_exception(out); throw; }
            catch (const thread::abort_thread&) { iostream_exception(out); throw; }
            catch (const abi::__forced_unwind&) { iostream_exception(out); throw; }
            catch (...)                         { iostream_exception(out); }
            return out;
        }

        friend std::istream& operator>>(std::istream& in, midi& out)
        {
            auto& rx { rx_state(in) };
            std::unique_lock<std::mutex> lock { rx.mutex };
            std::ios::iostate error { std::ios::goodbit };
            auto& buf = *in.rdbuf();
            auto i { rx.pending_msg.cbegin() };

            struct failure { };

            auto fail = [&error]
            {
                error |= std::ios::failbit;
                throw failure { };
            };

            auto get_any = [&]
            {
                if (i != rx.pending_msg.cend()) return *(i++);
                auto b = static_cast<byte>(buf.sbumpc());
                if (b >= 0xf8)  // system realtime messages may be mixed in
                {
                    out.time = clock::now();
                    switch (b)
                    {
                    case 0xf8: out.msg = clock_tick { }; break;
                    case 0xfa: out.msg = clock_start { }; break;
                    case 0xfb: out.msg = clock_continue { }; break;
                    case 0xfc: out.msg = clock_stop { }; break;
                    case 0xfe: out.msg = active_sense { }; break;
                    case 0xff: out.msg = reset { }; break;
                    default: [[unlikely]] fail();
                    }
                    throw system_realtime_message { };
                }
                rx.pending_msg.push_back(b);
                return b;
            };

            auto get = [&]
            {
                auto b = get_any();
                if ((b & 0x80) != 0) [[unlikely]] fail();
                return b;
            };

            std::istream::sentry sentry { in, true };
            try
            {
                if (not sentry) [[unlikely]] throw failure { };
                byte a { rx.last_status };

                if (rx.pending_msg.empty())
                {
                    if (a == 0)
                    {
                        while ((buf.sgetc() & 0x80) == 0) buf.sbumpc(); // discard data until the first status byte
                        a = get_any();
                    }
                    buf.sgetc();    // make sure there is data available before timestamping
                    rx.pending_msg_time = clock::now();
                }
                else
                {
                    if ((rx.pending_msg[0] & 0x80) != 0) a = get_any();
                }
                out.time = rx.pending_msg_time;

                if ((a & 0xf0) != 0xf0)   // channel message
                {
                    rx.last_status = a;
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
                    default: [[unlikely]] fail();
                    }
                }
                else                    // system message
                {
                    rx.last_status = 0;
                    switch (a)
                    {
                    case 0xf0:
                    {
                        auto& data = out.msg.emplace<sysex>().data;
                        data.reserve(32);
                        for (a = get_any(); a != 0xf7; a = get_any()) data.push_back(a);
                        break;
                    }
                    case 0xf1: out.msg = mtc_quarter_frame { { }, get() }; break;
                    case 0xf2: out.msg = song_position { { }, { get(), get() } }; break;
                    case 0xf3: out.msg = song_select { { }, get() }; break;
                    case 0xf6: out.msg = tune_request { }; break;
                    default: [[unlikely]] fail();
                    }
                }
                rx.pending_msg.clear();
            }
            catch (const failure&) { rx.pending_msg.clear(); }
            catch (const system_realtime_message&) { }
            catch (const terminate_exception&)  { iostream_exception(in); throw; }
            catch (const thread::abort_thread&) { iostream_exception(in); throw; }
            catch (const abi::__forced_unwind&) { iostream_exception(in); throw; }
            catch (...)                         { iostream_exception(in); }
            if (error != std::ios::goodbit) in.setstate(error);
            return in;
        }
    };
}
