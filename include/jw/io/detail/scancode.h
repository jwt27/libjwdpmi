/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2023 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <optional>
#include <jw/common.h>
#include <jw/io/key.h>
#include <jw/circular_queue.h>
#include "jwdpmi_config.h"

namespace jw::io
{
    enum scancode_set : byte
    {
        set1 = 1, set2, set3,
    };
}

namespace jw::io::detail
{
    // Single scancode
    using raw_scancode = std::uint8_t;

    using scancode_queue = circular_queue<raw_scancode, config::scancode_buffer_size, queue_sync::write_irq>;

    struct scancode
    {
        // Extract and decode one scancode sequence from a sequence of bytes
        // NOTE: parameter will be modified, extracted sequences are removed
        static std::optional<key_state_pair> extract(scancode_queue& bytes, scancode_set set)
        {
            key k = key::bad_key;
            key_state state = key_state::down;
            byte ext = 0;

            for (auto i = bytes.begin(); i != bytes.end(); ++i)
            {
                auto c = *i;
                if (set == set1 or set == set2)
                {
                    if ((c & 0xF0) == 0xE0) { ext = c; continue; }
                }
                if (set == set2 or set == set3)
                {
                    if (c == 0xF0) { state = key_state::up; continue; }
                }

                bytes.pop_to(i + 1);

                switch (set)
                {
                case set1:
                    if ((c & 0x80) != 0) { state = key_state::up; }
                    c = set1_to_set2_table[c & 0x7F];
                    [[fallthrough]];
                case set2:
                    if (ext == 0xE0)
                    {
                        if (c == 0x37) { k = key::pwr_on; break; }
                        if (c == 0x5E) { k = key::pwr_wake; break; }
                        if (auto p = set2_e0_to_set3_table[c]) { c = p; }
                        else { k = 0xE000 | c; break; }
                    }
                    else if (ext == 0xE1)
                    {
                        if (c == 0x14) { k = key::pause; break; }
                        else { k = 0xE100 | c; break; }
                    }
                    else if (ext != 0) { k = (ext << 8) | c; break; }
                    else if (auto p = set2_to_set3_table[c]) { c = p; }
                    [[fallthrough]];
                case set3:
                    k = set3_to_key_table[c];
                }
                if (k == key::bad_key) k = 0x0100 | c;
                return key_state_pair { k, state };
            }

            return std::nullopt;
        }

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
                if ((c & 0x80) != 0 and (c & 0xF0) != 0xE0)
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
        scancode() = delete;

        static const std::array<raw_scancode, 0x100> undo_translation_table;
        static const std::array<raw_scancode, 0x80> set1_to_set2_table;
        static const std::array<byte, 0x100> set2_to_set3_table;
        static const std::array<byte, 0x100> set2_e0_to_set3_table;
        static const std::array<byte, 0x100> set3_to_key_table;
    };
}
