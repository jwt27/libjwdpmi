/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */

#include <list>
#include <mutex>
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
        std::vector<byte> pending_msg { };
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

    template <typename S, typename T>
    void* get_pword(int i, S& stream, std::list<T>& list)
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

    struct midi_out
    {
        midi_out(std::ostream& o) : out { o }, buf { o.rdbuf() }, tx { tx_state(o) } { }

        void emit(const midi& in)
        {
            std::unique_lock lock { tx.mutex, std::defer_lock };
            if (not in.is_realtime_message()) lock.lock();
            std::ostream::sentry sentry { out };
            try
            {
                if (sentry) std::visit(*this, in.type);
            }
            catch (const terminate_exception&)  { throw; }
            catch (const thread::abort_thread&) { throw; }
            catch (const abi::__forced_unwind&) { throw; }
            catch (...) { out.setstate(std::ios::badbit); }
        }

        void operator()(const midi::no_message&) noexcept { };

        void operator()(const midi::channel_message& t)
        {
            std::visit([this, &t](auto&& msg) { (*this)(t.channel, msg); }, t.message);
        };

        void operator()(const midi::system_message& t)
        {
            clear_status();
            std::visit([this](auto&& msg) { (*this)(msg); }, t.message);
        };

        void operator()(const midi::realtime& t)
        {
            switch (t)
            {
            case midi::realtime::clock_tick:        return put_realtime(0xf8);
            case midi::realtime::clock_start:       return put_realtime(0xfa);
            case midi::realtime::clock_continue:    return put_realtime(0xfb);
            case midi::realtime::clock_stop:        return put_realtime(0xfc);
            case midi::realtime::active_sense:      return put_realtime(0xfe);
            case midi::realtime::reset:             return put_realtime(0xff);
            default: __builtin_unreachable();
            }
        };

        void operator()(byte ch, const midi::note_event& msg)
        {
            if (not msg.on and tx.last_status == (0x90 | ch))
            {
                put(msg.key);
                put(0x00);
            }
            else
            {
                put_status((msg.on ? 0x90 : 0x80) | ch);
                put(msg.key);
                put(msg.velocity);
            }
        }
        void operator()(byte ch, const midi::key_pressure& msg)     { put_status(0xa0 | ch); put(msg.key); put(msg.value); }
        void operator()(byte ch, const midi::control_change& msg)   { put_status(0xb0 | ch); put(msg.controller); put(msg.value); }
        void operator()(byte ch, const midi::program_change& msg)   { put_status(0xc0 | ch); put(msg.value); }
        void operator()(byte ch, const midi::channel_pressure& msg) { put_status(0xd0 | ch); put(msg.value); }
        void operator()(byte ch, const midi::pitch_change& msg)     { put_status(0xe0 | ch); put(msg.value.lo); put(msg.value.hi); }

        void operator()(byte ch, const midi::long_control_change& msg)
        {
            (*this)(ch, midi::control_change      { msg.controller, msg.value.hi });
            (*this)(ch, midi::control_change      { msg.controller + 0x20u, msg.value.lo });
        }

        void operator()(byte ch, const midi::rpn_change& msg)
        {
            (*this)(ch, midi::control_change      { 0x65u, msg.parameter.hi });
            (*this)(ch, midi::control_change      { 0x64u, msg.parameter.lo });
            (*this)(ch, midi::long_control_change { 0x06u, msg.value });
        }

        void operator()(byte ch, const midi::nrpn_change& msg)
        {
            (*this)(ch, midi::control_change      { 0x63u, msg.parameter.hi });
            (*this)(ch, midi::control_change      { 0x62u, msg.parameter.lo });
            (*this)(ch, midi::long_control_change { 0x06u, msg.value });
        }

        void operator()(const midi::sysex& msg)             { put(0xf0); buf->sputn(reinterpret_cast<const char*>(msg.data.data()), msg.data.size()); put(0xf7); }
        void operator()(const midi::mtc_quarter_frame& msg) { put(0xf1); put(msg.data); }
        void operator()(const midi::song_position& msg)     { put(0xf2); put(msg.value.lo); put(msg.value.hi); }
        void operator()(const midi::song_select& msg)       { put(0xf3); put(msg.value); }
        void operator()(const midi::tune_request&)          { put(0xf6); }

    private:
        void put_realtime(byte a)
        {
            if (auto* mpu = dynamic_cast<jw::io::detail::mpu401_streambuf*>(buf))
                mpu->put_realtime(a);
            else
                put(a);
        }

        void put_status(byte a)
        {
            if (tx.last_status != a) put(a);
            tx.last_status = a;
        }

        void put(byte a) { buf->sputc(a); }

        void clear_status() { tx.last_status = 0; }

        std::ostream& out;
        std::streambuf* const buf;
        ostream_info& tx;
    };

    void midi::emit(std::ostream& out) const
    {
        midi_out { out }.emit(*this);
    }

    midi midi::do_extract(std::istream& in, bool dont_block)
    {
        struct unexpected_status { };
        struct failure { };
        struct end_of_file { };

        constexpr auto is_status = [](byte b) { return (b & 0x80) != 0; };
        constexpr auto is_realtime = [](byte b) { return b > 0xf7; };

        constexpr auto realtime_msg = [](byte b, clock::time_point now = clock::now())
        {
            switch (b)
            {
            case 0xf8: return midi { realtime::clock_tick, now };
            case 0xfa: return midi { realtime::clock_start, now };
            case 0xfb: return midi { realtime::clock_continue, now };
            case 0xfc: return midi { realtime::clock_stop, now };
            case 0xfe: return midi { realtime::active_sense, now };
            case 0xff: return midi { realtime::reset, now };
            case 0xfd: throw failure { };
            default: __builtin_unreachable();
            }
        };

        constexpr auto channel_msg_index = [](byte status)
        {
            switch (status)
            {
            case 0x80:
            case 0x90: return channel_message::index_of<note_event>();
            case 0xa0: return channel_message::index_of<key_pressure>();
            case 0xb0: return channel_message::index_of<control_change>();
            case 0xc0: return channel_message::index_of<program_change>();
            case 0xd0: return channel_message::index_of<channel_pressure>();
            case 0xe0: return channel_message::index_of<pitch_change>();
            default: __builtin_unreachable();
            }
        };

        constexpr auto system_msg_index = [](byte status)
        {
            switch (status)
            {
            case 0xf0: return system_message::index_of<sysex>();
            case 0xf1: return system_message::index_of<mtc_quarter_frame>();
            case 0xf2: return system_message::index_of<song_position>();
            case 0xf3: return system_message::index_of<song_select>();
            case 0xf6: return system_message::index_of<tune_request>();
            case 0xf4:
            case 0xf5:
            case 0xf7: throw failure { };
            default: __builtin_unreachable();
            }
        };

        constexpr auto channel_msg_size = [](std::size_t i) -> std::size_t
        {
            switch (i)
            {
            case channel_message::index_of<note_event>():       return 2;
            case channel_message::index_of<key_pressure>():     return 2;
            case channel_message::index_of<channel_pressure>(): return 1;
            case channel_message::index_of<control_change>():   return 2;
            case channel_message::index_of<program_change>():   return 1;
            case channel_message::index_of<pitch_change>():     return 2;
            default: __builtin_unreachable();
            }
        };

        constexpr auto system_msg_size = [](std::size_t i) -> std::size_t
        {
            switch (i)
            {
            case system_message::index_of<sysex>():             return -2;
            case system_message::index_of<mtc_quarter_frame>(): return 1;
            case system_message::index_of<song_position>():     return 2;
            case system_message::index_of<song_select>():       return 1;
            case system_message::index_of<tune_request>():      return 0;
            default: __builtin_unreachable();
            }
        };

        auto& rx { rx_state(in) };
        std::unique_lock lock { rx.mutex };
        auto* const buf { in.rdbuf() };

        auto peek = [&]() -> std::optional<byte>
        {
            if (dont_block and buf->in_avail() == 0)
            {
                buf->pubsync();
                if (buf->in_avail() == 0) return { };
            }
            auto b = buf->sgetc();
            if (b == std::char_traits<char>::eof()) throw end_of_file { };
            return { static_cast<byte>(b) };
        };

        auto get = [&]
        {
            auto b = peek();
            if (b)
            {
                buf->sbumpc();
                if (not is_realtime(*b)) rx.pending_msg.push_back(*b);
            }
            return b;
        };

        std::istream::sentry sentry { in, true };
        try
        {
            if (not sentry) throw failure { };

            byte status = rx.last_status;

            // Wait for data to arrive
            if (rx.pending_msg.empty())
            {
                // Discard data until the first status byte
                if (status == 0) while (true)
                {
                    const auto b = peek();
                    if (not b) return { };
                    if (is_status(*b)) break;
                    buf->sbumpc();
                }
                const auto b = get();
                if (not b) return { };
                rx.pending_msg_time = clock::now();
                if (is_realtime(*b)) return realtime_msg(*b, rx.pending_msg_time);
            }

            // Check for new status byte
            bool new_status = false;
            if (is_status(rx.pending_msg.front()))
            {
                status = rx.pending_msg.front();
                new_status = true;
            }

            // Determine message type and size
            enum { system, channel } type;
            std::size_t index;
            std::size_t size;
            if ((status & 0xf0) != 0xf0)
            {
                type = channel;
                index = channel_msg_index(status & 0xf0);
                size = channel_msg_size(index);
            }
            else
            {
                type = system;
                index = system_msg_index(status);
                size = system_msg_size(index);
            }

            // Read bytes from streambuf
            const bool is_sysex = type == system and index == system_message::index_of<sysex>();
            while (rx.pending_msg.size() < size + new_status)
            {
                const auto b = get();
                if (not b) return { };
                if (is_realtime(*b)) return realtime_msg(*b);
                if (is_sysex and *b == 0xf7) break;
                if (is_status(*b)) [[unlikely]]
                {
                    rx.pending_msg_time = clock::now();
                    rx.pending_msg.clear();
                    rx.pending_msg.push_back(*b);
                    throw unexpected_status { };
                }
            }
            if (is_sysex and rx.pending_msg.size() - new_status == 1) throw failure { };

            // Construct the message
            const unsigned ch = status & 0x0f;
            const auto i = rx.pending_msg.cbegin() + new_status;
            auto at = [i](auto index) { return byte { *(i + index) }; };
            auto make_msg = [&rx]<typename... T>(T&&... args)
            {
                rx.pending_msg.clear();
                return midi { std::forward<T>(args)..., rx.pending_msg_time };
            };
            switch (type)
            {
            case channel:
                rx.last_status = status;
                switch (index)
                {
                case channel_message::index_of<note_event>():
                {
                    byte vel = at(1);
                    bool on = (status & 0x10) != 0;
                    if (on and vel == 0)
                    {
                        on = false;
                        vel = 0x40;
                    }
                    return make_msg(ch, note_event { at(0), vel, on });
                }
                case channel_message::index_of<key_pressure>():     return make_msg(ch, key_pressure     { at(0), at(1) });
                case channel_message::index_of<channel_pressure>(): return make_msg(ch, channel_pressure { at(0) });
                case channel_message::index_of<control_change>():   return make_msg(ch, control_change   { at(0), at(1) });
                case channel_message::index_of<program_change>():   return make_msg(ch, program_change   { at(0) });
                case channel_message::index_of<pitch_change>():     return make_msg(ch, pitch_change     { split_uint14_t { at(0), at(1) } });
                }
                break;

            case system:
                rx.last_status = 0;
                switch(index)
                {
                case system_message::index_of<sysex>():             return make_msg(sysex { std::vector<byte> { i, rx.pending_msg.cend() - 1 } });
                case system_message::index_of<mtc_quarter_frame>(): return make_msg(mtc_quarter_frame { at(0) });
                case system_message::index_of<song_position>():     return make_msg(song_position { split_uint14_t { at(0), at(1) } });
                case system_message::index_of<song_select>():       return make_msg(song_select { at(0) });
                case system_message::index_of<tune_request>():      return make_msg(tune_request { });
                }
            }
        }
        catch (const failure&)
        {
            rx.pending_msg.clear();
            rx.last_status = 0;
            in.setstate(std::ios::failbit);
        }
        catch (const unexpected_status&)    { in.setstate(std::ios::failbit); }
        catch (const end_of_file&)          { in.setstate(std::ios::eofbit); }
        catch (const terminate_exception&)  { throw; }
        catch (const thread::abort_thread&) { throw; }
        catch (const abi::__forced_unwind&) { throw; }
        catch (...)                         { in.setstate(std::ios::badbit); }
        return { };
    }
}
