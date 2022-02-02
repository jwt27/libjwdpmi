/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <variant>
#include <vector>
#include <iostream>
#include <optional>
#include <string>
#include <chrono>
#include <jw/common.h>
#include <jw/split_int.h>
#include <jw/specific_int.h>
#include <jw/chrono.h>
#include <jw/io/io_error.h>
#include <jw/variant.h>
#include "jwdpmi_config.h"

namespace jw
{
    using split_uint14_t = split_uint<14>;

    template <typename T>
    concept is_time_point = requires { typename T::clock; std::chrono::is_clock_v<typename T::clock>; };
}

namespace jw::midi
{
    using clock = jw::config::midi_clock;

    // Channel message sub-types
    struct note_event           { unsigned note    : 7, : 1, velocity : 7, : 1; bool on; };
    struct key_pressure         { unsigned note    : 7, : 1, value    : 7; };
    struct channel_pressure     { unsigned value   : 7; };
    struct control_change       { unsigned control : 7, : 1, value    : 7; };
    struct program_change       { unsigned value   : 7; };
    struct pitch_change         { split_uint14_t value; };

    // System Common message sub-types
    struct sysex                { std::vector<byte> data; };
    struct mtc_quarter_frame    { unsigned data : 7; };
    struct song_position        { split_uint14_t value; };
    struct song_select          { unsigned value : 7; };
    struct tune_request         { };

    // Channel message type
    struct channel_message
    {
        unsigned channel : 4, : 0;
        std::variant<note_event, key_pressure, channel_pressure,
                     control_change, program_change, pitch_change> message;

        template <typename T>
        static consteval bool contains() { return variant_contains<decltype(message), T>(); }
        template <typename T>
        static consteval std::size_t index_of() { return variant_index<decltype(message), T>(); }
    };

    // System Common message type
    struct system_message
    {
        std::variant<sysex, mtc_quarter_frame, song_position, song_select, tune_request> message;

        template <typename T>
        static consteval bool contains() { return variant_contains<decltype(message), T>(); }
        template <typename T>
        static consteval std::size_t index_of() { return variant_index<decltype(message), T>(); }
    };

    // System Realtime message type
    enum class realtime : byte
    {
        clock_tick      = 0,
        clock_start     = 2,
        clock_continue  = 3,
        clock_stop      = 4,
        active_sense    = 6,
        reset           = 7
    };
    using realtime_message = realtime;

    // Meta message type used in MIDI files
    struct meta
    {
        struct sequence_number { unsigned num : 16; };
        struct tempo_change { std::chrono::microseconds quarter_note; };
        struct smpte_offset
        {
            unsigned hour : 8;
            unsigned minute : 8;
            unsigned second : 8;
            unsigned frame : 8;
            unsigned fractional_frame : 8;  // 100ths of a frame
        };
        struct time_signature
        {
            unsigned numerator : 8;
            unsigned denominator : 8;   // 2 = 1/4 note, 3 = 1/8 note, etc.
            unsigned clocks_per_metronome_click : 8;
            unsigned notated_32nd_notes_per_24_clocks : 8;
        };
        struct key_signature
        {
            signed num_sharps : 4, : 0;
            bool major_key;
        };
        struct text
        {
            enum
            {
                any,
                copyright,
                track_name,
                instrument_name,
                lyric,
                marker,
                cue_point
            } type;
            std::string text;
        };
        struct unknown
        {
            unsigned type : 8, : 0;
            std::vector<byte> data;
        };

        std::optional<specific_uint<4>> channel;
        std::variant<unknown, sequence_number, text, tempo_change,
                     smpte_offset, time_signature, key_signature> message;

        template <typename T>
        static consteval bool contains() { return variant_contains<decltype(message), T>(); }
        template <typename T>
        static consteval std::size_t index_of() { return variant_index<decltype(message), T>(); }
    };

    // Copyable wrapper class for std::unique_ptr<meta>.  Meta messages
    // are large and relatively rare, so to keep sizeof(message) reasonable,
    // they are stored on the heap.
    struct meta_message
    {
        template<typename... Args>
        meta_message(Args&&... args) : ptr { std::make_unique<meta>(std::forward<Args>(args)...) } { }

        meta_message() = delete;
        ~meta_message() = default;
        meta_message(meta_message&&) noexcept = default;
        meta_message& operator=(meta_message&&) noexcept = default;
        meta_message(const meta_message& m) : ptr { copy_from(m) } { }
        meta_message& operator=(const meta_message& m);

        meta* get() const noexcept { return &*ptr; }
        meta& operator*() const { return *ptr; }
        meta* operator->() const noexcept { return get(); }
        bool valid() const noexcept { return static_cast<bool>(ptr);; }
        explicit operator bool() const noexcept { return valid(); }

    private:
        static std::unique_ptr<meta> copy_from(const meta_message& m);
        std::unique_ptr<meta> ptr;
    };

    // Represents any complete MIDI message with no time stamp.
    struct untimed_message
    {
        std::variant<std::monostate, channel_message, system_message, realtime_message, meta_message> category;

        template <typename T>
        static consteval std::size_t index_of() { return variant_index<decltype(category), T>(); }

        template<typename C, typename M> requires (channel_message::contains<std::remove_cvref_t<M>>())
        constexpr untimed_message(C&& ch, M&& m) noexcept : category { channel_message { std::forward<C>(ch), std::forward<M>(m) } } { }

        template<typename C, typename M> requires (meta::contains<std::remove_cvref_t<M>>())
        constexpr untimed_message(C&& ch, M&& m) : category { meta_message { std::forward<C>(ch), std::forward<M>(m) } } { }

        template<typename M> requires (meta::contains<std::remove_cvref_t<M>>())
        constexpr untimed_message(M&& m) : untimed_message { std::nullopt, std::forward<M>(m) } { }

        template<typename M> requires (system_message::contains<std::remove_cvref_t<M>>())
        constexpr untimed_message(M&& m) : category { system_message { std::forward<M>(m) } } { }

        template<typename M> requires std::same_as<realtime, std::remove_cvref_t<M>>
        constexpr untimed_message(M&& m) noexcept : category { std::forward<M>(m) } { }

        explicit untimed_message(std::istream& in);

        constexpr untimed_message() noexcept = default;
        untimed_message(const untimed_message&) noexcept = default;
        untimed_message(untimed_message&&) noexcept = default;
        untimed_message& operator=(const untimed_message&) noexcept = default;
        untimed_message& operator=(untimed_message&&) noexcept = default;

        bool valid() const noexcept;
        explicit operator bool() const noexcept { return valid(); };

        bool is_channel_message() const noexcept { return category.index() == index_of<channel_message>(); }
        bool is_system_message() const noexcept { return category.index() == index_of<system_message>(); }
        bool is_realtime_message() const noexcept { return category.index() == index_of<realtime_message>(); }
        bool is_meta_message() const noexcept { return category.index() == index_of<meta_message>(); }

        std::optional<specific_uint<4>> channel() const noexcept;
    };

    // Represents a complete MIDI message with time stamp.
    template<typename T>
    struct timed_message : untimed_message
    {
        using time_type = T;
        time_type time;

        template<typename C, typename M> requires std::constructible_from<untimed_message, C, M>
        timed_message(C&& ch, M&& m, const T& t = default_time()) : untimed_message { std::forward<C>(ch), std::forward<M>(m) }, time { t } { }

        template<typename M> requires (std::constructible_from<untimed_message, M> and not std::derived_from<std::remove_cvref_t<M>, std::istream>)
        timed_message(M&& m, const T& t = default_time()) : untimed_message { std::forward<M>(m) }, time { t } { }

        explicit timed_message(std::istream& in);

        constexpr timed_message() noexcept = default;
        timed_message(const timed_message&) noexcept = default;
        timed_message(timed_message&&) noexcept = default;
        timed_message& operator=(const timed_message&) noexcept = default;
        timed_message& operator=(timed_message&&) noexcept = default;

        template<typename U = T> requires (is_time_point<U>)
        static U default_time() { return T::clock::now(); }
        template<typename U = T> requires (not is_time_point<U>)
        static U default_time() { return { }; }
    };

    // Unified time-stamped MIDI message type, using the clock specified in
    // the configuration header.  Reading from an istream produces messages of
    // this type.
    using message = timed_message<clock::time_point>;

    // Write a MIDI message to the ostream, taking running status into account.
    void emit(std::ostream& out, const untimed_message& msg);

    // Extract one time-stamped MIDI message from the specified istream.
    // Blocks until a complete message is received.
    message extract(std::istream& in);

    // Extract one time-stamped MIDI message from the specified istream,
    // returning an empty message immediately if not enough bytes are
    // available yet.
    message try_extract(std::istream& in);

    inline std::ostream& operator<<(std::ostream& out, const untimed_message& in) { emit(out, in); return out; }
    inline std::istream& operator>>(std::istream& in, untimed_message& out) { out = extract(in); return in; }
    inline std::istream& operator>>(std::istream& in, message& out) { out = extract(in); return in; }

    template<typename T = clock::time_point>
    inline std::array<timed_message<T>, 2> long_control_change(specific_uint<4> ch, specific_uint<7> control, split_uint14_t value, const T& time = timed_message<T>::default_time())
    {
        using D = T::duration;
        return { timed_message<T> { static_cast<unsigned>(ch), control_change { static_cast<unsigned>(control + 0x00), value.hi }, time + D { 0 } },
                 timed_message<T> { static_cast<unsigned>(ch), control_change { static_cast<unsigned>(control + 0x20), value.lo }, time + D { 1 } } };
    }

    template<typename T = clock::time_point>
    inline std::array<timed_message<T>, 4> rpn_change(specific_uint<4> ch, split_uint14_t param, split_uint14_t value, const T& time = timed_message<T>::default_time())
    {
        using D = T::duration;
        return { timed_message<T> { static_cast<unsigned>(ch), control_change { 0x65, param.hi }, time + D { 0 } },
                 timed_message<T> { static_cast<unsigned>(ch), control_change { 0x64, param.lo }, time + D { 1 } },
                 timed_message<T> { static_cast<unsigned>(ch), control_change { 0x06, value.hi }, time + D { 2 } },
                 timed_message<T> { static_cast<unsigned>(ch), control_change { 0x26, value.lo }, time + D { 3 } } };
    }

    template<typename T = clock::time_point>
    inline std::array<timed_message<T>, 4> nrpn_change(specific_uint<4> ch, split_uint14_t param, split_uint14_t value, const T& time = timed_message<T>::default_time())
    {
        using D = T::duration;
        return { timed_message<T> { static_cast<unsigned>(ch), control_change { 0x63, param.hi }, time + D { 0 } },
                 timed_message<T> { static_cast<unsigned>(ch), control_change { 0x62, param.lo }, time + D { 1 } },
                 timed_message<T> { static_cast<unsigned>(ch), control_change { 0x06, value.hi }, time + D { 2 } },
                 timed_message<T> { static_cast<unsigned>(ch), control_change { 0x26, value.lo }, time + D { 3 } } };
    }

    template<typename T>
    inline timed_message<T>::timed_message(std::istream& in) : timed_message { extract(in) } { }
    inline untimed_message::untimed_message(std::istream& in) : untimed_message { extract(in) } { }

    inline bool untimed_message::valid() const noexcept
    {
        if (auto* p = std::get_if<meta_message>(&category)) return p->valid();
        return category.index() != index_of<std::monostate>();
    }

    inline std::optional<specific_uint<4>> untimed_message::channel() const noexcept
    {
        if (auto* t = std::get_if<channel_message>(&category))
            return { t->channel };
        if (auto* t = std::get_if<meta_message>(&category))
            if (*t) return (*t)->channel;
        return { };
    }

    inline meta_message& meta_message::operator=(const meta_message& m)
    {
        if (ptr and m.ptr) *ptr = *m.ptr;
        else if (not ptr and m.ptr) ptr = std::make_unique<meta>(*m.ptr);
        else if (ptr and not m.ptr) *ptr = meta { };
        return *this;
    }

    inline std::unique_ptr<meta> meta_message::copy_from(const meta_message& m)
    {
        if (m.ptr) return std::make_unique<meta>(*m.ptr);
        else return std::make_unique<meta>();
    }
}
