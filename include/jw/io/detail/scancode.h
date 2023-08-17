/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2016 - 2023 J.W. Jagersma, see COPYING.txt for details    */

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

    using scancode_queue = static_circular_queue<raw_scancode, config::scancode_buffer_size, queue_sync::producer_irq>;

    struct scancode
    {
        // Extract and decode one scancode sequence from a sequence of bytes
        // NOTE: parameter will be modified, extracted sequences are removed
        static std::optional<key_state_pair> extract(scancode_queue::consumer_type*, scancode_set);

        // Undo scancode translation for a single byte. No break code handling.
        static raw_scancode undo_translation(raw_scancode c) noexcept { return undo_translation_table[c]; }

        // Undo scancode translation and insert break codes on a sequence of bytes. This behaves much like std::back_inserter.
        template<typename Container>
        static auto undo_translation_inserter(Container& c) { return undo_translation_iterator { c }; }

        template<typename C>
        struct undo_translation_iterator
        {
            using container_type = C;
            using value_type = void;
            using difference_type = std::ptrdiff_t;
            using pointer = void;
            using reference = void;
            using iterator_category = std::output_iterator_tag;

            explicit undo_translation_iterator(C& c) : container(std::addressof(c)) { }
            undo_translation_iterator(const undo_translation_iterator&) = default;
            undo_translation_iterator(undo_translation_iterator&&) = default;

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
    };
}
