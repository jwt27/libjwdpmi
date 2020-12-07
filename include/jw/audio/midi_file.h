/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <vector>
#include <deque>
#include <fstream>
#include <filesystem>
#include <jw/audio/midi.h>

namespace jw::audio
{
    struct midi_file
    {
        using track = std::deque<midi>;

        struct smpte_format
        {
            unsigned frames_per_second : 7, : 0;    // note: 29 means 29.97 fps
            unsigned clocks_per_frame : 8;
        };

        midi_file(std::istream& stream) : midi_file { read(stream) } { }
        midi_file(const std::filesystem::path& file) : midi_file { read(file) } { }

        midi_file() noexcept = default;
        midi_file(const midi_file&) = default;
        midi_file(midi_file&&) noexcept = default;
        midi_file& operator=(const midi_file&) = default;
        midi_file& operator=(midi_file&&) noexcept = default;

        static midi_file read(std::istream&);
        static midi_file read(const std::filesystem::path& file)
        {
            std::ifstream stream { file, std::ios::in | std::ios::binary };
            stream.exceptions(std::ios::badbit | std::ios::eofbit);
            return read(stream);
        }

        bool asynchronous_tracks;
        std::variant<unsigned, smpte_format> time_division;
        std::vector<track> tracks;
    };

    inline std::istream& operator>>(std::istream& in, midi_file& out) { out = midi_file::read(in); return in; }
}
