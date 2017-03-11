/******************************* libjwdpmi **********************************
Copyright (C) 2016-2017  J.W. Jagersma

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once
#include <deque>

#include <jw/typedef.h>
#include <jw/io/key.h>

namespace jw
{
    namespace io
    {
            // Single scancode
        using raw_scancode = byte;

        enum scancode_set : byte
        {
            set1 = 1, set2, set3,
        };

        // Scancode sequence
        class scancode
        {
        public:
            // Extract full scancode sequences from a sequence of bytes
            // NOTE: parameter will be modified, extracted sequences are removed
            static std::deque<scancode> extract(auto& codes, scancode_set set)
            {
                std::deque<scancode> extracted_codes;

                auto i = codes.begin();
                while (i != codes.end())
                {
                    if (set == set2)
                    {
                        if (*i == 0xE0) { ++i; continue; }
                        if (*i == 0xE1) { ++i; continue; }
                    }
                    if (*i == 0xF0) { ++i; continue; }
                    i++;
                    extracted_codes.push_back({ set, { codes.begin(), i } });
                    codes.erase(codes.begin(), i);
                    i = codes.begin();
                }

                return extracted_codes;
            }

            // Decode scancode sequence into key code and state pair
            key_state_pair decode();

            // Undo scancode translation for a single byte. No break code handling.
            static raw_scancode undo_translation(raw_scancode c) noexcept { return undo_translation_table[c]; }

            // Undo scancode translation and insert break codes on a sequence of bytes. This behaves much like std::back_inserter.
            template<typename Container>
            static auto undo_translation_inserter(Container& c) { return undo_translation_iterator<Container>(std::addressof(c)); }

            template<typename C>
            struct undo_translation_iterator
            {
                using value_type = void;
                using difference_type = void;
                using pointer = void;
                using reference = void;
                using iterator_category = std::output_iterator_tag;

                undo_translation_iterator(C* c) : container(c) { }

                auto& operator=(raw_scancode c)
                {
                    if ((c & 0x80) && (c != 0xE0) && (c != 0xE1))
                    {
                        container->push_back(0xF0);
                        c &= 0x7F;
                    }
                    container->push_back(undo_translation(c));
                    return *this;
                }

                auto& operator*() { return *this; }
                auto& operator++() { return *this; }
                auto& operator++(int) { return *this; }

            private:
                C* container;
            };

        private:
            std::deque<raw_scancode> sequence;
            scancode_set code_set;

            scancode(scancode_set set, std::deque<raw_scancode> seq)
                : sequence(seq), code_set(set) { }

            scancode(scancode_set set)
                : scancode(set, { }) { }

            static const std::array<raw_scancode, 0x100> undo_translation_table;
            static std::unordered_map<raw_scancode, raw_scancode> set2_to_set3_table;
            static std::unordered_map<raw_scancode, const raw_scancode> set2_extended0_to_set3_table;
            static std::unordered_map<raw_scancode, const raw_scancode> set2_extended0_to_key_table;
            static std::unordered_map<raw_scancode, key> set3_to_key_table;
        };
    }
}