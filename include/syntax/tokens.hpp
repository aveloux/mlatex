#pragma once

#include "syntax/catcodes.hpp"
#include "syntax/lexicon.hpp"
#include "memory/location.hpp"

#include <string_view>
#include <type_traits>

namespace syntax {

    /// @brief One lexed character or control sequence.
    ///
    /// The lexer produces one token per character, so a numeral such as `12`
    /// is two tokens; scanners read multi-token quantities digit by digit.
    /// Tokens are trivially copyable and copied freely.
    struct Token {
        Symbol symbol = none;                                         ///< Interned name, or #none.
        CatCodes::Category category = CatCodes::Category::Invalid;    ///< Category when lexed.
        memory::Location location{};                                  ///< Source position.
        std::string_view text{};                                      ///< Interned text.

        /// @brief Reports whether this is the end-of-input token.
        [[nodiscard]] constexpr bool empty() const noexcept { return symbol == none && text.empty(); }

        /// @brief Tests for one character of one category.
        /// @param want      Required category.
        /// @param character Required character.
        [[nodiscard]] constexpr bool is(const CatCodes::Category want, const char character) const noexcept {
            return category == want && text.size() == 1 && text[0] == character;
        }

        /// @brief Tests for one character of any category.
        /// @param character Required character.
        [[nodiscard]] constexpr bool is(const char character) const noexcept {
            return text.size() == 1 && text[0] == character;
        }

        /// Deleted: two tokens with the same text may differ in category and
        /// position. Compare #symbol, or use is().
        bool operator==(const Token&) const = delete("compare the symbols, or use is(): two tokens with the same text may differ in category");
    };

    static_assert(std::is_trivially_copyable_v<Token>);

}
