/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <vector>
#include <deque>
#include <fstream>
#include <filesystem>
#include <jw/audio/midi.h>

namespace jw::midi
{
    struct file
    {
        using track = std::deque<timed_message<unsigned>>;

        struct smpte_format
        {
            unsigned frames_per_second : 7, : 0;    // note: 29 means 29.97 fps
            unsigned clocks_per_frame : 8;
        };

        file(std::istream& stream) : file { read(stream) } { }
        file(const std::filesystem::path& f) : file { read(f) } { }

        file() noexcept = default;
        file(const file&) = default;
        file(file&&) noexcept = default;
        file& operator=(const file&) = default;
        file& operator=(file&&) noexcept = default;

        static file read(std::istream&);
        static file read(const std::filesystem::path& file)
        {
            std::ifstream stream { file, std::ios::in | std::ios::binary };
            stream.exceptions(std::ios::badbit | std::ios::failbit | std::ios::eofbit);
            return read(stream);
        }

        bool asynchronous_tracks;
        std::variant<unsigned, smpte_format> time_division;
        std::vector<track> tracks;
    };

    inline std::istream& operator>>(std::istream& in, file& out) { out = file::read(in); return in; }
}
