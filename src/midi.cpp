/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */

#include <list>
#include <mutex>
#include <deque>
#include <cxxabi.h>
#include <jw/audio/midi.h>
#include <jw/thread/thread.h>
#include <jw/thread/mutex.h>
#include <jw/io/mpu401.h>

namespace jw::audio
{
    struct istream_info
    {
        thread::mutex mutex { };
        std::deque<byte> pending_msg { };
        midi::clock::time_point pending_msg_time;
        byte last_status { 0 };
    };
    struct ostream_info
    {
        thread::mutex mutex { };
        byte last_status { 0 };
    };
    std::list<istream_info> istream_list { };
    std::list<ostream_info> ostream_list { };

    template <typename S, typename L>
    void* get_pword(int i, S& stream, L& list)
    {
        void*& p = stream.pword(i);
        if (p == nullptr) [[unlikely]]
            p = &list.emplace_back();
        return p;
    }

    istream_info& rx_state(std::istream& stream)
    {
        static const int i = std::ios_base::xalloc();
        return *static_cast<istream_info*>(get_pword(i, stream, istream_list));
    }

    ostream_info& tx_state(std::ostream& stream)
    {
        static const int i = std::ios_base::xalloc();
        return *static_cast<ostream_info*>(get_pword(i, stream, ostream_list));
    }

    template <typename S>
    void iostream_exception(S& stream)
    {
        stream.setstate(std::ios::badbit);
        if (stream.exceptions() & stream.rdstate()) throw;
    };

    struct midi_out
    {
        midi_out(std::ostream& o) : out { o }, buf { o.rdbuf() }, tx { tx_state(o) } { }

        void emit(const midi& in)
        {
            std::unique_lock<thread::mutex> lock { tx.mutex, std::defer_lock };
            if (not in.is_realtime_message()) lock.lock();
            std::ostream::sentry sentry { out };
            try
            {
                if (sentry) std::visit(*this, in.msg);
            }
            catch (const terminate_exception&)  { iostream_exception(out); throw; }
            catch (const thread::abort_thread&) { iostream_exception(out); throw; }
            catch (const abi::__forced_unwind&) { iostream_exception(out); throw; }
            catch (...)                         { iostream_exception(out); }
        }

        void operator()(const midi::note_event& msg)
        {
            if (not msg.on and tx.last_status == (0x90 | (msg.channel & 0x0f)))
            {
                put_status(0x90 | (msg.channel & 0x0f));
                buf->sputc(msg.key);
                buf->sputc(0x00);
            }
            else
            {
                put_status((msg.on ? 0x90 : 0x80) | (msg.channel & 0x0f));
                buf->sputc(msg.key);
                buf->sputc(msg.velocity);
            }
        }
        void operator()(const midi::key_pressure& msg)        { put_status(0xa0 | (msg.channel & 0x0f)); buf->sputc(msg.key); buf->sputc(msg.value); }
        void operator()(const midi::control_change& msg)      { put_status(0xb0 | (msg.channel & 0x0f)); buf->sputc(msg.controller); buf->sputc(msg.value); }
        void operator()(const midi::program_change& msg)      { put_status(0xc0 | (msg.channel & 0x0f)); buf->sputc(msg.value); }
        void operator()(const midi::channel_pressure& msg)    { put_status(0xd0 | (msg.channel & 0x0f)); buf->sputc(msg.value); }
        void operator()(const midi::pitch_change& msg)        { put_status(0xe0 | (msg.channel & 0x0f)); buf->sputc(msg.value.lo); buf->sputc(msg.value.hi); }

        void operator()(const midi::sysex& msg)               { clear_status(); buf->sputc(0xf0); buf->sputn(reinterpret_cast<const char*>(msg.data.data()), msg.data.size()); buf->sputc(0xf7); }
        void operator()(const midi::mtc_quarter_frame& msg)   { clear_status(); buf->sputc(0xf1); buf->sputc(msg.data); }
        void operator()(const midi::song_position& msg)       { clear_status(); buf->sputc(0xf2); buf->sputc(msg.value.lo); buf->sputc(msg.value.hi); }
        void operator()(const midi::song_select& msg)         { clear_status(); buf->sputc(0xf3); buf->sputc(msg.value); }
        void operator()(const midi::tune_request&)            { clear_status(); buf->sputc(0xf6); }

        void operator()(const midi::clock_tick&)              { put_realtime(0xf8); }
        void operator()(const midi::clock_start&)             { put_realtime(0xfa); }
        void operator()(const midi::clock_continue&)          { put_realtime(0xfb); }
        void operator()(const midi::clock_stop&)              { put_realtime(0xfc); }
        void operator()(const midi::active_sense&)            { put_realtime(0xfe); }
        void operator()(const midi::reset&)                   { put_realtime(0xff); }

        void operator()(const midi::long_control_change& msg)
        {
            (*this)(midi::control_change { { msg.channel }, msg.controller, static_cast<byte>(msg.value.hi) });
            (*this)(midi::control_change { { msg.channel }, static_cast<byte>(msg.controller + 0x20), static_cast<byte>(msg.value.lo) });
        }

        void operator()(const midi::rpn_change& msg)
        {
            (*this)(midi::control_change { { msg.channel }, 0x65, static_cast<byte>(msg.parameter.hi) });
            (*this)(midi::control_change { { msg.channel }, 0x64, static_cast<byte>(msg.parameter.lo) });
            (*this)(midi::long_control_change { { msg.channel }, 0x06, msg.value });
        }

        void operator()(const midi::nrpn_change& msg)
        {
            (*this)(midi::control_change { { msg.channel }, 0x63, static_cast<byte>(msg.parameter.hi) });
            (*this)(midi::control_change { { msg.channel }, 0x62, static_cast<byte>(msg.parameter.lo) });
            (*this)(midi::long_control_change { { msg.channel }, 0x06, msg.value });
        }

    private:
        void put_realtime(byte a)
        {
            if (auto* mpu = dynamic_cast<jw::io::detail::mpu401_streambuf*>(buf))
                mpu->put_realtime(a);
            else
                buf->sputc(a);
        }

        void put_status(byte a)
        {
            if (tx.last_status != a) buf->sputc(a);
            tx.last_status = a;
        }

        void clear_status() { tx.last_status = 0; }

        std::ostream& out;
        std::streambuf* const buf;
        ostream_info& tx;
    };

    midi extract_midi(std::istream& in)
    {
        midi out;
        auto& rx { rx_state(in) };
        std::unique_lock<thread::mutex> lock { rx.mutex };
        std::ios::iostate error { std::ios::goodbit };
        auto* const buf = in.rdbuf();
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
            auto b = static_cast<byte>(buf->sbumpc());
            if (b >= 0xf8)  // system realtime messages may be mixed in
            {
                out.time = midi::clock::now();
                switch (b)
                {
                case 0xf8: out.msg = midi::clock_tick { }; break;
                case 0xfa: out.msg = midi::clock_start { }; break;
                case 0xfb: out.msg = midi::clock_continue { }; break;
                case 0xfc: out.msg = midi::clock_stop { }; break;
                case 0xfe: out.msg = midi::active_sense { }; break;
                case 0xff: out.msg = midi::reset { }; break;
                default: [[unlikely]] fail();
                }
                throw midi::system_realtime_message { };
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
                    while ((buf->sgetc() & 0x80) == 0) buf->sbumpc(); // discard data until the first status byte
                    a = get_any();
                }
                buf->sgetc();    // make sure there is data available before timestamping
                rx.pending_msg_time = midi::clock::now();
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
                case 0x80: out.msg = midi::note_event { { ch }, false, get(), get() }; break;
                case 0x90:
                {
                    auto key = get();
                    auto vel = get();
                    if (vel == 0) out.msg = midi::note_event { { ch }, false, key, vel };
                    else out.msg = midi::note_event { { ch }, true, key, vel };
                    break;
                }
                case 0xa0: out.msg = midi::key_pressure { { ch }, get(), get() }; break;
                case 0xb0: out.msg = midi::control_change { { ch }, get(), get() }; break;
                case 0xc0: out.msg = midi::program_change { { ch }, get() }; break;
                case 0xd0: out.msg = midi::channel_pressure { { ch }, get() }; break;
                case 0xe0: out.msg = midi::pitch_change { { ch }, { get(), get() } }; break;
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
                    auto& data = out.msg.emplace<midi::sysex>().data;
                    data.reserve(32);
                    for (a = get_any(); a != 0xf7; a = get_any()) data.push_back(a);
                    break;
                }
                case 0xf1: out.msg = midi::mtc_quarter_frame { { }, get() }; break;
                case 0xf2: out.msg = midi::song_position { { }, { get(), get() } }; break;
                case 0xf3: out.msg = midi::song_select { { }, get() }; break;
                case 0xf6: out.msg = midi::tune_request { }; break;
                default: [[unlikely]] fail();
                }
            }
            rx.pending_msg.clear();
        }
        catch (const failure&) { rx.pending_msg.clear(); }
        catch (const midi::system_realtime_message&) { }
        catch (const terminate_exception&)  { iostream_exception(in); throw; }
        catch (const thread::abort_thread&) { iostream_exception(in); throw; }
        catch (const abi::__forced_unwind&) { iostream_exception(in); throw; }
        catch (...)                         { iostream_exception(in); }
        if (error != std::ios::goodbit) in.setstate(error);
        return out;
    }

    void midi::emit(std::ostream& out) const
    {
        midi_out { out }.emit(*this);
    }

    midi midi::extract(std::istream& in)
    {
        return extract_midi(in);
    }
}
