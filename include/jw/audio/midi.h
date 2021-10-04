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
#include <../jwdpmi_config.h>

namespace jw
{
    using split_uint14_t = split_uint<14>;

    template <typename V, typename T, std::size_t I = 0>
    consteval bool variant_contains()
    {
        if constexpr (I >= std::variant_size_v<V>) return false;
        else if constexpr (std::is_same_v<T, std::variant_alternative_t<I, V>>) return true;
        else return variant_contains<V, T, I + 1>();
    }

    template <typename V, typename T, std::size_t I = 0>
    consteval std::size_t variant_index()
    {
        static_assert(variant_contains<V, T>());
        if constexpr (not variant_contains<V, T>()) return std::variant_npos;
        else if constexpr (std::is_same_v<T, std::variant_alternative_t<I, V>>) return I;
        else return variant_index<V, T, I + 1>();
    }

    template<std::size_t I = 0, typename F, typename V>
    constexpr decltype(auto) visit(F&& visitor, V&& variant)
    {
        constexpr auto size = std::variant_size_v<std::remove_cvref_t<V>>;
        if (auto* p = std::get_if<I>(&std::forward<V>(variant)))
            return std::forward<F>(visitor)(*p);
        else if constexpr (I + 1 < size)
            return visit<I + 1>(std::forward<F>(visitor), std::forward<V>(variant));
        throw std::bad_variant_access { };
    }
}

namespace jw::audio
{
    struct midi
    {
        using clock = jw::config::midi_clock;

        // Channel message sub-types
        struct note_event           { unsigned note    : 7, : 0, velocity : 7, : 0; bool on; };
        struct key_pressure         { unsigned note    : 7, : 0, value    : 7; };
        struct channel_pressure     { unsigned value   : 7; };
        struct control_change       { unsigned control : 7, : 0, value    : 7; };
        struct long_control_change  { unsigned control : 7, : 0; split_uint14_t value; }; // never received
        struct program_change       { unsigned value   : 7; };
        struct pitch_change         { split_uint14_t value; };
        struct rpn_change           { split_uint14_t parameter, value; };    // never received
        struct nrpn_change          { split_uint14_t parameter, value; };    // never received

        // System Common message sub-types
        struct sysex                { std::vector<byte> data; };
        struct mtc_quarter_frame    { unsigned data : 7; };     // TODO
        struct song_position        { split_uint14_t value; };
        struct song_select          { unsigned value : 7; };
        struct tune_request         { };

        // Channel message type
        struct channel_message
        {
            unsigned channel : 4, : 0;
            std::variant<note_event, key_pressure, channel_pressure, control_change,
                long_control_change, program_change, pitch_change, rpn_change, nrpn_change> message;

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

        template <typename T>
        static consteval std::size_t index_of() { return variant_index<decltype(type), T>(); }

        // MIDI Message
        std::variant<std::monostate, channel_message, system_message, realtime, meta> type;
        // Time - either a clock tick count, relative offset duration, or absolute time_point.
        std::variant<unsigned, clock::duration, clock::time_point> time;

        template<typename C, typename M, typename T> requires (channel_message::contains<M>())
        constexpr midi(C&& ch, M&& m, T&& t) noexcept : type { channel_message { std::forward<C>(ch), std::forward<M>(m) } }, time { std::forward<T>(t) } { }

        template<typename C, typename M, typename T> requires (meta::contains<M>())
        constexpr midi(C&& ch, M&& m, T&& t) noexcept : type { meta { std::forward<C>(ch), std::forward<M>(m) } }, time { std::forward<T>(t) } { }

        template<typename M, typename T> requires (meta::contains<M>())
        constexpr midi(M&& m, T&& t) noexcept : midi { std::nullopt, std::forward<M>(m), std::forward<T>(t) } { }

        template<typename M, typename T> requires (system_message::contains<M>())
        constexpr midi(M&& m, T&& t) noexcept : type { system_message { std::forward<M>(m) } }, time { std::forward<T>(t) } { }

        template<typename M, typename T> requires std::same_as<realtime, M>
        constexpr midi(M&& m, T&& t) noexcept : type { std::forward<M>(m) }, time { std::forward<T>(t) } { }

        template<typename C, typename M>
        constexpr midi(C&& ch, M&& m) noexcept : midi { std::forward<C>(ch), std::forward<M>(m), clock::time_point { } } { }

        template<typename M>
        constexpr midi(M&& m) noexcept : midi { std::forward<M>(m), clock::time_point { } } { }

        template<typename S> requires std::derived_from<S, std::istream>
        explicit constexpr midi(S& in) : midi { extract(in) } { }

        constexpr midi() noexcept = default;
        midi(const midi&) noexcept = default;
        midi(midi&&) noexcept = default;
        midi& operator=(const midi&) noexcept = default;
        midi& operator=(midi&&) noexcept = default;

        bool valid() const noexcept { return type.index() != index_of<std::monostate>() and type.index() != std::variant_npos; }
        explicit operator bool() const noexcept { return valid(); };

        bool is_channel_message() const noexcept { return type.index() == index_of<channel_message>(); }
        bool is_system_message() const noexcept { return type.index() == index_of<system_message>(); }
        bool is_realtime_message() const noexcept { return type.index() == index_of<realtime>(); }
        bool is_meta_message() const noexcept { return type.index() == index_of<meta>(); }

        std::optional<unsigned> channel() const
        {
            if (auto* t = std::get_if<channel_message>(&type))
                return { t->channel };
            return { };
        }

        void emit(std::ostream& out) const;
        static midi extract(std::istream& in) { return do_extract(in, false); }
        static midi try_extract(std::istream& in) { return do_extract(in, true); }

    private:
        static midi do_extract(std::istream&, bool);
    };

    inline std::ostream& operator<<(std::ostream& out, const midi& in) { in.emit(out); return out; }
    inline std::istream& operator>>(std::istream& in, midi& out) { out = midi::extract(in); return in; }
}
