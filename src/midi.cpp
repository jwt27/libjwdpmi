/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */

#include <list>
#include <mutex>
#include <cxxabi.h>
#include <jw/audio/midi.h>
#include <jw/audio/midi_file.h>
#include <jw/thread/thread.h>
#include <jw/thread/mutex.h>
#include <jw/io/realtime_streambuf.h>

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
        bool realtime { false };
    };
    std::list<istream_info> istream_list { };
    std::list<ostream_info> ostream_list { };

    template <typename S, typename T>
    static void* get_pword(int i, S& stream, std::list<T>& list)
    {
        void*& p = stream.pword(i);
        if (p == nullptr) [[unlikely]]
        {
            p = &list.emplace_back();
            if constexpr (std::is_same_v<T, ostream_info>)
                if (dynamic_cast<io::realtime_streambuf*>(stream.rdbuf()) != nullptr)
                    static_cast<ostream_info*>(p)->realtime = true;
        }
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

        void operator()(const std::monostate&) noexcept { };
        void operator()(const midi::meta&) noexcept { };

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
                put(msg.note);
                put(0x00);
            }
            else
            {
                put_status((msg.on ? 0x90 : 0x80) | ch);
                put(msg.note);
                put(msg.velocity);
            }
        }
        void operator()(byte ch, const midi::key_pressure& msg)     { put_status(0xa0 | ch); put(msg.note); put(msg.value); }
        void operator()(byte ch, const midi::control_change& msg)   { put_status(0xb0 | ch); put(msg.control); put(msg.value); }
        void operator()(byte ch, const midi::program_change& msg)   { put_status(0xc0 | ch); put(msg.value); }
        void operator()(byte ch, const midi::channel_pressure& msg) { put_status(0xd0 | ch); put(msg.value); }
        void operator()(byte ch, const midi::pitch_change& msg)     { put_status(0xe0 | ch); put(msg.value.lo); put(msg.value.hi); }

        void operator()(byte ch, const midi::long_control_change& msg)
        {
            (*this)(ch, midi::control_change      { msg.control, msg.value.hi });
            (*this)(ch, midi::control_change      { msg.control + 0x20u, msg.value.lo });
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

        void operator()(const midi::sysex& msg)             { buf->sputn(reinterpret_cast<const char*>(msg.data.data()), msg.data.size()); }
        void operator()(const midi::mtc_quarter_frame& msg) { put(0xf1); put(msg.data); }
        void operator()(const midi::song_position& msg)     { put(0xf2); put(msg.value.lo); put(msg.value.hi); }
        void operator()(const midi::song_select& msg)       { put(0xf3); put(msg.value); }
        void operator()(const midi::tune_request&)          { put(0xf6); }

    private:
        void put_realtime(byte a)
        {
            if (tx.realtime) static_cast<jw::io::realtime_streambuf*>(buf)->put_realtime(a);
            else put(a);
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

    struct unexpected_status { };
    struct failure { };
    struct end_of_file { };

    static constexpr bool is_status(byte b) { return (b & 0x80) != 0; };
    static constexpr bool is_realtime(byte b) { return b >= 0xf8; };
    static constexpr bool is_system(byte b) { return b >= 0xf0; };

    static constexpr std::size_t msg_size(byte status)
    {
        switch(status & 0xf0)
        {
        case 0x80:
        case 0x90: return 2;
        case 0xa0: return 2;
        case 0xb0: return 2;
        case 0xc0: return 1;
        case 0xd0: return 1;
        case 0xe0: return 2;
        case 0xf0:
            switch (status)
            {
            case 0xf0: return -2;
            case 0xf1: return 1;
            case 0xf2: return 2;
            case 0xf3: return 1;
            case 0xf6: return 0;
            case 0xf4:
            case 0xf5:
            case 0xf7:
            case 0xf9:
            case 0xfd: throw failure { };
            default: return 0;
            }
        default: __builtin_unreachable();
        }
    };

    template<typename T>
    static midi realtime_msg(byte status, T now)
    {
        switch (status)
        {
        case 0xf8: return midi { midi::realtime::clock_tick, now };
        case 0xfa: return midi { midi::realtime::clock_start, now };
        case 0xfb: return midi { midi::realtime::clock_continue, now };
        case 0xfc: return midi { midi::realtime::clock_stop, now };
        case 0xfe: return midi { midi::realtime::active_sense, now };
        case 0xff: return midi { midi::realtime::reset, now };
        case 0xf9:
        case 0xfd: throw failure { };
        default: __builtin_unreachable();
        }
    };

    template<typename I, typename T>
    static auto make_msg(byte status, I i, T now)
    {
        const unsigned ch = status & 0x0f;
        switch(status & 0xf0)
        {
        case 0x80:
        case 0x90:
        {
            byte vel = i[1];
            bool on = (status & 0x10) != 0;
            if (on and vel == 0)
            {
                on = false;
                vel = 0x40;
            }
            return midi { ch, midi::note_event { i[0], vel, on } };
        }
        case 0xa0: return midi { ch, midi::key_pressure     { i[0], i[1] }, now };
        case 0xb0: return midi { ch, midi::control_change   { i[0], i[1] }, now };
        case 0xc0: return midi { ch, midi::program_change   { i[0] }, now };
        case 0xd0: return midi { ch, midi::channel_pressure { i[0] }, now };
        case 0xe0: return midi { ch, midi::pitch_change     { { i[0], i[1] } }, now };
        case 0xf0:
            switch (status)
            {
            case 0xf0: __builtin_unreachable();
            case 0xf1: return midi { midi::mtc_quarter_frame { i[0] }, now };
            case 0xf2: return midi { midi::song_position     { { i[0], i[1] } }, now };
            case 0xf3: return midi { midi::song_select       { i[0] }, now };
            case 0xf6: return midi { midi::tune_request      { }, now };
            case 0xf4:
            case 0xf5:
            case 0xf7: throw failure { };
            default: return realtime_msg(status, now);
            }
        default: __builtin_unreachable();
        }
    }

    midi midi::do_extract(std::istream& in, bool dont_block)
    {
        auto& rx { rx_state(in) };
        std::unique_lock lock { rx.mutex };
        auto* const buf { in.rdbuf() };
        std::istream::sentry sentry { in, true };
        if (not sentry) return { };

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

        try
        {
            byte status = rx.last_status;

            // Wait for data to arrive
            if (rx.pending_msg.empty())
            {
                // Discard data until the first status byte
                if (status == 0) while (true)
                {
                    const auto b = peek();
                    if (not b) return { };
                    if (is_status(*b) and *b != 0xf7) break;
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

            // Read bytes from streambuf
            const bool is_sysex = status == 0xf0;
            while (rx.pending_msg.size() < msg_size(status) + new_status)
            {
                const auto b = get();
                if (not b) return { };
                if (is_realtime(*b)) return realtime_msg(*b, clock::now());
                if (is_status(*b))
                {
                    if (is_sysex and *b == 0xf7) break;
                    rx.pending_msg_time = clock::now();
                    rx.pending_msg.clear();
                    rx.pending_msg.push_back(*b);
                    throw unexpected_status { };
                }
            }

            // Store running status
            if (is_system(status)) rx.last_status = 0;
            else rx.last_status = status;

            struct guard
            {
                ~guard() { rx.pending_msg.clear(); }
                istream_info& rx;
            } clear_pending_on_return { rx };

            // Construct the message
            if (is_sysex) return midi { midi::sysex { { rx.pending_msg.cbegin(), rx.pending_msg.cend() } } };
            else return make_msg(status, rx.pending_msg.cbegin() + new_status, rx.pending_msg_time);
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

    struct file_buffer
    {
        file_buffer(std::streambuf* buf, std::size_t s)
            : size { s }, data { new byte[size] }, i { begin() }
        {
            const std::size_t bytes_read = buf->sgetn(reinterpret_cast<char*>(data.get()), size);
            if (bytes_read < size) throw end_of_file { };
        }

        template<typename T>
        void read(T* dst, std::size_t n)
        {
            if (i + n > end()) throw failure { };
            for (unsigned j = 0; j < n; ++j)
                reinterpret_cast<byte*>(dst)[j] = i[j];
            i += n;
            return;
        }

        std::uint32_t read_32()
        {
            union
            {
                std::array<byte, 4> raw;
                std::uint32_t value;
            };
            read(raw.data(), 4);
            return __builtin_bswap32(value);
        }

        std::uint32_t read_24()
        {
            union
            {
                std::array<byte, 4> raw;
                std::uint32_t value;
            };
            raw[0] = 0;
            read(raw.data() + 1, 3);
            return __builtin_bswap32(value);
        }

        std::uint16_t read_16()
        {
            union
            {
                std::array<byte, 2> raw;
                std::uint16_t value;
            };
            read(raw.data(), 2);
            return __builtin_bswap16(value);
        }

        std::uint8_t read_8()
        {
            if (i == end()) throw failure { };
            return *i++;
        }

        std::uint32_t read_vlq()
        {
            std::uint32_t value { };
            byte b;
            do
            {
                b = read_8();
                value <<= 7;
                value |= b & 0x7f;
            } while ((b & 0x80) != 0);
            return value;
        }

        const byte* begin() const noexcept { return data.get(); }
        const byte* end() const noexcept { return data.get() + size; }

    private:
        const std::size_t size;
        std::unique_ptr<byte[]> data;
        const byte* i;
    };

    static std::size_t find_chunk(std::streambuf* buf, std::string_view want)
    {
        auto read = [buf](char* data, std::size_t size)
        {
            if (size == 0) return;
            const std::size_t bytes_read = buf->sgetn(data, size);
            if (bytes_read < size) throw end_of_file { };
        };

        auto read_32 = [read]()
        {
            union
            {
                std::array<char, 4> raw;
                std::uint32_t value;
            };
            read(raw.data(), 4);
            return __builtin_bswap32(value);
        };

        std::array<char, 4> raw;
        do
        {
            read(raw.data(), 4);
            const std::size_t size = read_32();
            const std::string_view have { raw.data(), 4 };
            if (have == want) return size;
            buf->pubseekoff(size, std::ios::cur, std::ios::in);
        } while (true);
    }

    static auto text_type(byte type) noexcept
    {
        switch (type)
        {
        case 0x01: return midi::meta::text::any;
        case 0x02: return midi::meta::text::copyright;
        case 0x03: return midi::meta::text::track_name;
        case 0x04: return midi::meta::text::instrument_name;
        case 0x05: return midi::meta::text::lyric;
        case 0x06: return midi::meta::text::marker;
        case 0x07: return midi::meta::text::cue_point;
        default: __builtin_unreachable();
        }
    }

    static void read_track(midi_file::track& trk, file_buffer& buf)
    {
        std::array<byte, 8> v;
        bool in_sysex = false;
        byte last_status = 0;
        decltype(midi::meta::channel) meta_ch { };
        while (true)
        {
            const unsigned delta = buf.read_vlq();
            const byte b = buf.read_8();
            switch (b)
            {
            case 0xff:  // Meta message
                {
                    last_status = 0;
                    const byte type = buf.read_8();
                    const std::size_t size = buf.read_vlq();
                    switch (type)
                    {
                    case 0x00:
                        if (size != 2) throw failure { };
                        trk.emplace_back(meta_ch, midi::meta::sequence_number { buf.read_16() });
                        break;

                    case 0x01: case 0x02: case 0x03: case 0x04:
                    case 0x05: case 0x06: case 0x07:
                        {
                            midi::meta::text msg { text_type(type), { } };
                            msg.text.resize(size);
                            buf.read(msg.text.data(), size);
                            trk.emplace_back(meta_ch, std::move(msg), delta);
                            break;
                        }

                    case 0x20:
                        {
                            if (size != 1) throw failure { };
                            auto ch = buf.read_8();
                            if (ch > 15) throw failure { };
                            meta_ch = ch;
                            break;
                        }

                    case 0x2f:
                        return;

                    case 0x51:
                        if (size != 3) throw failure { };
                        trk.emplace_back(meta_ch, midi::meta::tempo_change { std::chrono::microseconds { buf.read_24() } }, delta);
                        break;

                    case 0x54:
                        {
                            if (size != 5) throw failure { };
                            buf.read(v.data(), 5);
                            trk.emplace_back(meta_ch, midi::meta::smpte_offset { v[0], v[1], v[2], v[3], v[4] }, delta);
                            break;
                        }

                    case 0x58:
                        {
                            if (size != 4) throw failure { };
                            buf.read(v.data(), 4);
                            trk.emplace_back(meta_ch, midi::meta::time_signature { v[0], v[1], v[2], v[3] }, delta);
                            break;
                        }

                    case 0x59:
                        {
                            if (size != 2) throw failure { };
                            buf.read(v.data(), 2);
                            trk.emplace_back(meta_ch, midi::meta::key_signature { v[0], v[1] != 0 }, delta);
                            break;
                        }

                    default:
                        {
                            midi::meta::unknown msg { type, { } };
                            msg.data.resize(size);
                            buf.read(msg.data.data(), size);
                            trk.emplace_back(meta_ch, std::move(msg), delta);
                            break;
                        }
                    }
                    break;
                }

            case 0xf7:  // Either a sysex, part of a sysex, or escape sequence (may be any message, or multiple)
                {
                    last_status = 0;
                    meta_ch.reset();
                    std::vector<byte> data { };
                    const std::size_t size = buf.read_vlq();
                    data.reserve(size);
                    for (unsigned i = 0; i < size; ++i)
                    {
                        const byte b = buf.read_8();
                        data.push_back(b);

                        switch (b)
                        {
                        case 0xf0:
                            last_status = 0;
                            in_sysex = true;
                            for (; i < size; ++i)
                            {
                                const byte b = buf.read_8();
                                data.push_back(b);
                                if (b == 0xf7) break;
                            }
                            if (i == size) break;
                            [[fallthrough]];
                        case 0xf7:
                            trk.emplace_back(midi::sysex { std::move(data) }, delta);
                            if (i < size)
                            {
                                data = { };
                                data.reserve(size - i);
                            }
                            last_status = 0;
                            in_sysex = false;
                            break;

                        default:
                            if (not in_sysex)
                            {
                                byte status = last_status;
                                if (is_status(b)) status = b;
                                if (status == 0) throw failure { };

                                for (unsigned j = not is_status(b); j < msg_size(status); ++i, ++j)
                                {
                                    if (i == size) throw failure { };
                                    const byte b = buf.read_8();
                                    data.push_back(b);
                                }

                                if (not is_realtime(status))
                                {
                                    if (is_system(status)) last_status = 0;
                                    else last_status = status;
                                }

                                trk.emplace_back(make_msg(status, data.data() + is_status(b), delta));
                                data.clear();
                            }
                        }
                    }
                    if (data.size() > 0) trk.emplace_back(midi::sysex { std::move(data) }, delta);
                    last_status = 0;
                    break;
                }

            case 0xf0:  // Complete sysex or first part of a timed sysex
                {
                    last_status = 0;
                    meta_ch.reset();
                    const std::size_t size = buf.read_vlq();
                    midi::sysex msg { };
                    msg.data.push_back(0xf0);
                    msg.data.resize(size + 1);
                    buf.read(msg.data.data(), size);
                    in_sysex = true;
                    if (msg.data.back() == 0xf7) in_sysex = false;
                    trk.emplace_back(std::move(msg), delta);
                    break;
                }

            default:    // Channel message
                {
                    in_sysex = false;
                    meta_ch.reset();
                    auto* i = v.data();
                    byte status = last_status;
                    if (is_status(b)) status = b;
                    else *i++ = b;
                    switch (status)
                    {
                    case 0x00: case 0xf0: case 0xf7:
                        throw failure { };
                    }

                    const std::size_t size = msg_size(status);
                    if (size > 0) buf.read(i, size - is_status(b));

                    // Also accept realtime and system messages here (non-standard)
                    if (not is_realtime(status))
                    {
                        if (is_system(status)) last_status = 0;
                        else last_status = status;
                    }

                    trk.emplace_back(make_msg(status, i, delta));
                    break;
                }
            }
        }
    }

    midi_file midi_file::read(std::istream& in)
    {
        midi_file output { };
        auto* const rdbuf { in.rdbuf() };
        std::istream::sentry sentry { in, true };
        if (not sentry) return output;

        try
        {
            file_buffer buf { rdbuf, find_chunk(rdbuf, "MThd") };
            const std::uint16_t format = buf.read_16();
            const std::size_t num_tracks = buf.read_16();
            const split_uint16_t division = buf.read_16();

            if (format == 0 and num_tracks != 1) throw failure { };
            if (format > 2) throw failure { };
            output.asynchronous_tracks = format == 2;
            output.tracks.resize(num_tracks);

            if ((division & 0x8000) == 0) output.time_division.emplace<unsigned>(division);
            else output.time_division.emplace<smpte_format>(-static_cast<int8_t>(division.hi), division.lo);

            for (auto& trk : output.tracks)
            {
                file_buffer buf { rdbuf, find_chunk(rdbuf, "MTrk") };
                read_track(trk, buf);
            }
        }
        catch (const failure&)              { in.setstate(std::ios::failbit); }
        catch (const unexpected_status&)    { in.setstate(std::ios::failbit); }
        catch (const end_of_file&)          { in.setstate(std::ios::eofbit); }
        catch (const terminate_exception&)  { throw; }
        catch (const thread::abort_thread&) { throw; }
        catch (const abi::__forced_unwind&) { throw; }
        catch (...)                         { in.setstate(std::ios::badbit); }
        return output;
    }
}
