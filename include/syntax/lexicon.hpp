#pragma once

#include "memory/arena.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace syntax {

    /// @brief Interned name. Two control sequences are the same exactly when
    ///        their symbols are equal; the text is kept only for diagnostics.
    using Symbol = std::uint32_t;

    /// @brief The symbol no name interns to; it also marks the end of input.
    inline constexpr Symbol none = 0;

    /// @brief Interning table for control sequence and character names.
    ///
    /// Every distinct name is stored once in the arena and addressed by a
    /// dense Symbol, so every later table in the engine can index by symbol
    /// instead of hashing text. One-byte names, which are most of what a
    /// document contains, are resolved through a direct table; longer names
    /// through an open-addressed hash table.
    ///
    /// @par Use
    /// @code
    /// syntax::Lexicon lexicon(arena);
    /// const syntax::Symbol begin = lexicon.intern("\\begin");
    /// assert(lexicon.intern("\\begin") == begin);
    /// assert(lexicon.resolve(begin) == "\\begin");
    /// @endcode
    class Lexicon {
    public:
        /// @brief Creates an empty table.
        /// @param arena Allocator for name text; must outlive the table.
        explicit Lexicon(memory::Arena& arena);

        Lexicon(const Lexicon&) = delete("a symbol is an index into one table, and means nothing in a copy of it");
        Lexicon& operator=(const Lexicon&) = delete("a symbol is an index into one table, and means nothing in a copy of it");

        /// @brief Interns a name.
        /// @param name Text to intern.
        /// @return Its symbol, or #none for empty text.
        /// @complexity O(1) for one byte; O(n) in the length of @p name otherwise.
        Symbol intern(std::string_view name);

        /// @brief Recovers the text behind a symbol.
        /// @param symbol Symbol to resolve.
        /// @return Its text, or empty text for an unknown symbol.
        /// @complexity O(1).
        [[nodiscard]] std::string_view resolve(const Symbol symbol) const noexcept {
            return symbol < names.size() ? names[symbol] : std::string_view{};
        }

        /// @brief Number of symbols issued, #none included.
        [[nodiscard]] std::size_t size() const noexcept { return names.size(); }

    private:
        memory::Arena& arena;                  ///< Storage for name text.
        std::vector<std::string_view> names;   ///< Text of each symbol.
        std::vector<Symbol> table;             ///< Open-addressed slots for longer names; #none is empty.
        std::array<Symbol, 256> singles{};     ///< Symbol of each one-byte name.
    };

}
