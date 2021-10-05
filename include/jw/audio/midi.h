/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
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
#include <jw/common.h>
#include <jw/split_int.h>
#include <jw/specific_int.h>
#include <jw/chrono.h>
#include <jw/io/io_error.h>
#include <jw/variant.h>
#include <../jwdpmi_config.h>

namespace jw
{
    using split_uint14_t = split_uint<14>;
}

namespace jw::audio
{
    struct midi
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
        // are large and relatively rare, so to keep sizeof(midi) reasonable,
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

        template <typename T>
        static consteval std::size_t index_of() { return variant_index<decltype(type), T>(); }

        // MIDI Message
        std::variant<std::monostate, channel_message, system_message, realtime_message, meta_message> type;
        // Time - either a clock tick count, relative offset duration, or absolute time_point.
        std::variant<unsigned, clock::duration, clock::time_point> time;

        template<typename C, typename M, typename T> requires (channel_message::contains<M>())
        constexpr midi(C&& ch, M&& m, T&& t) noexcept : type { channel_message { std::forward<C>(ch), std::forward<M>(m) } }, time { std::forward<T>(t) } { }

        template<typename C, typename M, typename T> requires (meta::contains<M>())
        constexpr midi(C&& ch, M&& m, T&& t) : type { meta_message { std::forward<C>(ch), std::forward<M>(m) } }, time { std::forward<T>(t) } { }

        template<typename M, typename T> requires (meta::contains<M>())
        constexpr midi(M&& m, T&& t) : midi { std::nullopt, std::forward<M>(m), std::forward<T>(t) } { }

        template<typename M, typename T> requires (system_message::contains<M>())
        constexpr midi(M&& m, T&& t) : type { system_message { std::forward<M>(m) } }, time { std::forward<T>(t) } { }

        template<typename M, typename T> requires std::same_as<realtime, M>
        constexpr midi(M&& m, T&& t) noexcept : type { std::forward<M>(m) }, time { std::forward<T>(t) } { }

        template<typename C, typename M>
        constexpr midi(C&& ch, M&& m) : midi { std::forward<C>(ch), std::forward<M>(m), clock::time_point { } } { }

        template<typename M>
        constexpr midi(M&& m) noexcept : midi { std::forward<M>(m), clock::time_point { } } { }

        template<typename S> requires std::derived_from<S, std::istream>
        explicit constexpr midi(S& in) : midi { extract(in) } { }

        constexpr midi() noexcept = default;
        midi(const midi&) noexcept = default;
        midi(midi&&) noexcept = default;
        midi& operator=(const midi&) noexcept = default;
        midi& operator=(midi&&) noexcept = default;

        bool valid() const noexcept;
        explicit operator bool() const noexcept { return valid(); };

        bool is_channel_message() const noexcept { return type.index() == index_of<channel_message>(); }
        bool is_system_message() const noexcept { return type.index() == index_of<system_message>(); }
        bool is_realtime_message() const noexcept { return type.index() == index_of<realtime_message>(); }
        bool is_meta_message() const noexcept { return type.index() == index_of<meta_message>(); }

        std::optional<specific_uint<4>> channel() const noexcept;

        template<typename T>
        static std::array<midi, 2> long_control_change(specific_uint<4> ch, specific_uint<7> control, split_uint14_t value, T&& time);
        static std::array<midi, 2> long_control_change(specific_uint<4> ch, specific_uint<7> control, split_uint14_t value);

        template<typename T>
        static std::array<midi, 4> rpn_change(specific_uint<4> ch, split_uint14_t param, split_uint14_t value, T&& time);
        static std::array<midi, 4> rpn_change(specific_uint<4> ch, split_uint14_t param, split_uint14_t value);

        template<typename T>
        static std::array<midi, 4> nrpn_change(specific_uint<4> ch, split_uint14_t param, split_uint14_t value, T&& time);
        static std::array<midi, 4> nrpn_change(specific_uint<4> ch, split_uint14_t param, split_uint14_t value);

        void emit(std::ostream& out) const;
        static midi extract(std::istream& in) { return do_extract(in, false); }
        static midi try_extract(std::istream& in) { return do_extract(in, true); }

    private:
        static midi do_extract(std::istream&, bool);
    };

    inline bool midi::valid() const noexcept
    {
        switch (type.index())
        {
        case index_of<channel_message>():
        case index_of<system_message>():
        case index_of<realtime_message>():
            return true;

        case index_of<meta_message>():
            return std::get_if<meta_message>(&type)->valid();

        default:
            return false;
        }
    }

    inline std::optional<specific_uint<4>> midi::channel() const noexcept
    {
        if (auto* t = std::get_if<channel_message>(&type))
            return { t->channel };
        if (auto* t = std::get_if<meta_message>(&type))
            if (*t) return (*t)->channel;
        return { };
    }

    inline std::ostream& operator<<(std::ostream& out, const midi& in) { in.emit(out); return out; }
    inline std::istream& operator>>(std::istream& in, midi& out) { out = midi::extract(in); return in; }

    template<typename T>
    inline std::array<midi, 2> midi::long_control_change(specific_uint<4> ch, specific_uint<7> control, split_uint14_t value, T&& time)
    {
        return { midi { static_cast<unsigned>(ch), control_change { static_cast<unsigned>(control + 0x00), value.hi }, std::forward<T>(time) },
                 midi { static_cast<unsigned>(ch), control_change { static_cast<unsigned>(control + 0x20), value.lo }, std::forward<T>(time) }, };
    }

    template<typename T>
    inline std::array<midi, 4> midi::rpn_change(specific_uint<4> ch, split_uint14_t param, split_uint14_t value, T&& time)
    {
        return { midi { static_cast<unsigned>(ch), control_change { 0x65, param.hi }, std::forward<T>(time) },
                 midi { static_cast<unsigned>(ch), control_change { 0x64, param.lo }, std::forward<T>(time) },
                 midi { static_cast<unsigned>(ch), control_change { 0x06, value.hi }, std::forward<T>(time) },
                 midi { static_cast<unsigned>(ch), control_change { 0x26, value.lo }, std::forward<T>(time) }, };
    }

    template<typename T>
    inline std::array<midi, 4> midi::nrpn_change(specific_uint<4> ch, split_uint14_t param, split_uint14_t value, T&& time)
    {
        return { midi { static_cast<unsigned>(ch), control_change { 0x63, param.hi }, std::forward<T>(time) },
                 midi { static_cast<unsigned>(ch), control_change { 0x62, param.lo }, std::forward<T>(time) },
                 midi { static_cast<unsigned>(ch), control_change { 0x06, value.hi }, std::forward<T>(time) },
                 midi { static_cast<unsigned>(ch), control_change { 0x26, value.lo }, std::forward<T>(time) }, };
    }

    inline std::array<midi, 2> midi::long_control_change(specific_uint<4> ch, specific_uint<7> control, split_uint14_t value)
    {
        return long_control_change(ch, control, value, clock::now());
    }

    inline std::array<midi, 4> midi::rpn_change(specific_uint<4> ch, split_uint14_t param, split_uint14_t value)
    {
        return rpn_change(ch, param, value, clock::now());
    }

    inline std::array<midi, 4> midi::nrpn_change(specific_uint<4> ch, split_uint14_t param, split_uint14_t value)
    {
        return nrpn_change(ch, param, value, clock::now());
    }

    inline midi::meta_message& midi::meta_message::operator=(const meta_message& m)
    {
        if (ptr and m.ptr) *ptr = *m.ptr;
        else if (not ptr and m.ptr) ptr = std::make_unique<meta>(*m.ptr);
        else if (ptr and not m.ptr) *ptr = meta { };
        return *this;
    }

    inline std::unique_ptr<midi::meta> midi::meta_message::copy_from(const meta_message& m)
    {
        if (m.ptr) return std::make_unique<meta>(*m.ptr);
        else return std::make_unique<meta>();
    }
}
