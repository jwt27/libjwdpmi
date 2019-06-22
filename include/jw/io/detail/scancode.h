/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <deque>

#include <jw/common.h>
#include <jw/io/key.h>
#include <jw/io/io_error.h>

namespace jw
{
    namespace io
    {
        enum scancode_set : byte
        {
            set1 = 1, set2, set3,
        };

        namespace detail
        {
        // Single scancode
            using raw_scancode = byte;

            // Scancode sequence
            class scancode
            {
            public:
                // Extract one scancode sequence from a sequence of bytes
                // NOTE: parameter will be modified, extracted sequences are removed
                template <typename Container>
                static std::optional<scancode> extract(Container& bytes, scancode_set set)
                {
                    std::optional<scancode> extracted_code { };

                    for (auto i = bytes.begin(); i != bytes.end(); ++i)
                    {
                        if (set == set2)
                        {
                            if (*i == 0xE0) continue;
                            if (*i == 0xE1) continue;
                        }
                        if (*i == 0xF0) continue;
                        ++i;
                        extracted_code = scancode { set, bytes.begin(), i };
                        bytes.erase(bytes.begin(), i);
                        break;
                    }

                    return extracted_code;
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
                std::array<raw_scancode, 3> sequence;
                scancode_set code_set;

                scancode(scancode_set set, const std::array<raw_scancode, 3>& copy)
                    : sequence(copy), code_set(set) { }

                template <typename I0, typename I1>
                scancode(scancode_set set, I0 begin, I1 end) : code_set(set)
                {
                    if (end - begin > 3) [[unlikely]] throw framing_error { "Scancode longer than 3 bytes?" };
                    std::copy(begin, end, sequence.begin());
                }

                scancode(scancode_set set) : scancode(set, { }) { }

                static const std::array<raw_scancode, 0x100> undo_translation_table;
                static std::unordered_map<raw_scancode, raw_scancode> set2_to_set3_table;
                static std::unordered_map<raw_scancode, const raw_scancode> set2_extended0_to_set3_table;
                static std::unordered_map<raw_scancode, const raw_scancode> set2_extended0_to_key_table;
                static std::unordered_map<raw_scancode, key> set3_to_key_table;
            };
        }
    }
}
