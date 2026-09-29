#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace memory {

    /// @brief Named texts fixed at compile time, found in O(1).
    ///
    /// What the engine carries inside itself -- the prelude and its packages,
    /// a language's hyphenation patterns -- is embedded file by file with
    /// `#embed` and listed here under the name it is asked for by. The index
    /// is an open-addressed hash table built by the compiler: twice as many
    /// slots as names, so a lookup hashes the name once and almost always
    /// finds it in the first slot it tries. Nothing is built at run time.
    ///
    /// @par Use
    /// @code
    /// static constexpr char prelude[] = {
    /// #embed "main.mtex" suffix(,)
    /// 0};
    ///
    /// static constexpr memory::Catalog<1> catalog{{{
    ///     {"main.mtex", {prelude, sizeof prelude - 1}},
    /// }}};
    ///
    /// const std::optional<std::string_view> text = catalog.find("main.mtex");
    /// @endcode
    ///
    /// @tparam Count How many entries it holds.
    template <std::size_t Count>
    class Catalog {
    public:
        static_assert(Count < 0xFFFF, "a catalog indexes its entries in sixteen bits");

        /// @brief One entry: a name, and the text it stands for.
        struct Entry {
            std::string_view name{};   ///< What it is found by.
            std::string_view text{};   ///< What it holds.
        };

        /// @brief Indexes the entries; run by the compiler, never at run time.
        /// @param listing The entries. A name listed twice is found as its first.
        consteval explicit Catalog(const std::array<Entry, Count>& listing) : entries(listing) {
            slots.fill(empty);
            for (std::size_t index = 0; index < Count; ++index) {
                std::size_t at = hash(entries[index].name) & mask;
                while (slots[at] != empty) at = (at + 1) & mask;
                slots[at] = static_cast<std::uint16_t>(index);
            }
        }

        /// @brief The text listed under a name.
        /// @param name What to look for.
        /// @return The text, or std::nullopt when nothing is listed under it.
        /// @complexity O(1) expected: one hash, then the slots up to the first empty one.
        [[nodiscard]] constexpr std::optional<std::string_view> find(const std::string_view name) const noexcept {
            for (std::size_t at = hash(name) & mask;; at = (at + 1) & mask) {
                const std::uint16_t index = slots[at];
                if (index == empty) return std::nullopt;
                if (entries[index].name == name) return entries[index].text;
            }
        }

        /// @brief How many entries it holds.
        [[nodiscard]] static constexpr std::size_t size() noexcept { return Count; }

        /// @brief Every entry, in the order they were listed.
        [[nodiscard]] constexpr const std::array<Entry, Count>& listing() const noexcept { return entries; }

    private:
        /// @brief FNV-1a: one multiply per byte, and good enough spread for
        ///        names that differ in a letter or two.
        /// @param name The text to hash.
        /// @return Its hash.
        [[nodiscard]] static constexpr std::uint64_t hash(const std::string_view name) noexcept {
            std::uint64_t value = 0xCBF29CE484222325ull;
            for (const char letter : name) {
                value ^= static_cast<unsigned char>(letter);
                value *= 0x100000001B3ull;
            }
            return value;
        }

        /// Twice the entries, rounded up to a power of two, so there is always
        /// an empty slot for a search to stop at and the mask replaces a modulo.
        static constexpr std::size_t capacity = std::bit_ceil(Count * 2 + 1);
        static constexpr std::size_t mask = capacity - 1;     ///< Slot index from a hash.
        static constexpr std::uint16_t empty = 0xFFFF;        ///< A slot with no entry.

        std::array<Entry, Count> entries{};                   ///< The entries, as listed.
        std::array<std::uint16_t, capacity> slots{};          ///< Entry index by hash; #empty for none.
    };

}
