#pragma once

#include "layout/node.hpp"
#include "memory/arena.hpp"
#include "memory/slice.hpp"
#include "typography/font.hpp"

#include <string_view>

namespace render::layout {

    /// @brief Runs of text already shaped, so they are shaped once.
    ///
    /// Shaping is the expensive part of setting a paragraph, and a document
    /// repeats text more than it looks: running heads, repeated labels, and
    /// every paragraph again on a second layout pass. Keying on the text, the
    /// font and the size catches all of those.
    ///
    /// @par Open addressing
    /// Entries live in one flat array and a collision walks to the next slot.
    /// There are no buckets to chase and no allocation per entry, which is
    /// what makes a hit a single cache line rather than a pointer chase.
    ///
    /// @par Use
    /// @code
    /// const layout::Ledger::Key key{.font = font, .text = text, .size = 12.0f};
    /// memory::Slice<layout::Node*> glyphs = ledger.find(key);
    /// if (glyphs.empty()) {
    ///     glyphs = shaper.shape(fonts, text, {});
    ///     ledger.insert(key, glyphs);
    /// }
    /// @endcode
    class Ledger {
    public:
        /// @brief What identifies a shaped run.
        struct Key {
            const typography::Font* font{nullptr};   ///< Font it was shaped in.
            std::string_view text{};                 ///< The text itself.
            float size{0.0f};                        ///< Size in points.

            /// @brief Do two keys name the same run?
            [[nodiscard]] bool operator==(const Key& other) const noexcept;
        };

        /// @brief One slot in the table.
        struct Entry {
            Key key{};                      ///< What was shaped.
            memory::Slice<Node*> nodes{};   ///< The glyphs it produced.
            bool occupied{false};           ///< False for a slot never filled.
        };

        /// @brief Builds an empty cache.
        /// @param arena Allocator for the table.
        /// @param size    How many slots it has. A table three quarters full
        ///                stops caching rather than growing, so nothing already
        ///                handed out is ever moved -- and a lookup that misses
        ///                still meets an empty slot within a few probes, where
        ///                a table filled to the last slot would have it walk
        ///                every one of them.
        explicit Ledger(memory::Arena& arena, std::size_t size = 2048) noexcept;

        /// @brief Looks a run up.
        /// @param key What is wanted.
        /// @return Its glyphs, or an empty slice when it has not been shaped.
        /// @complexity O(1) average.
        [[nodiscard]] memory::Slice<Node*> find(const Key& key) const noexcept;

        /// @brief Records a shaped run.
        /// @param key   What was shaped.
        /// @param nodes The glyphs it produced.
        /// @complexity O(1) average; a full table does nothing.
        void insert(const Key& key, memory::Slice<Node*> nodes) noexcept;

        /// @brief Forgets every run, keeping the table for the next ones.
        ///
        /// For when whatever produced the runs has changed -- a document told
        /// to hyphenate differently from then on -- so that nothing shaped
        /// under the old rule is handed out under the new one.
        ///
        /// @complexity O(n) in the slots.
        void dispose() noexcept;

    private:
        /// @brief Hashes a key.
        /// @param key Key to hash.
        /// @complexity O(n) in the text's length.
        [[nodiscard]] static std::size_t digest(const Key& key) noexcept;

        std::size_t limit{0};            ///< Slots, always a power of two.
        std::size_t entries{0};          ///< Slots filled.
        memory::Slice<Entry> slots{};    ///< The table.
    };

}
