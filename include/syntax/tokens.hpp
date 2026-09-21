#pragma once

#include "syntax/catcodes.hpp"
#include "syntax/lexicon.hpp"
#include "memory/location.hpp"

#include <string_view>
#include <type_traits>

namespace syntax {

    /// @brief One lexed character or control sequence.
    ///
    /// The lexer emits exactly one Token per source character, so a numeral
    /// like `12` is two tokens and a unit like `pt` is two tokens. Scanners
    /// that expect to find a whole word inside `values` are wrong; see Number
    /// for how multi-token quantities are read.
    ///
    /// Trivially copyable by design: tokens are copied constantly, and Cursor
    /// stores them by value.
    struct Token {
        Symbol symbol = kInvalidSymbol;                                     ///< Interned name, or kInvalidSymbol.
        CatCodes::Category category = CatCodes::Category::Invalid;          ///< Category at the moment it was lexed.
        memory::Location location{};                                        ///< Where it came from, for diagnostics.
        std::string_view values{};                                          ///< Text, owned by the arena behind Lexicon.

        /// @brief Is this the end-of-stream token?
        /// @return True when the token carries neither text nor a symbol.
        /// @complexity O(1).
        [[nodiscard]] constexpr bool empty() const noexcept {
            return this->values.empty() && this->symbol == kInvalidSymbol;
        }

        /// @brief Tests for a single character of a given category.
        ///
        /// The shape nearly every call site wants: `token.is(Category::Group, '{')`
        /// rather than three separate conditions that are easy to get wrong.
        ///
        /// @param want      Category the token must carry.
        /// @param character  The single character it must hold.
        /// @return True when both match.
        /// @complexity O(1).
        [[nodiscard]] constexpr bool is(const CatCodes::Category want, const char character) const noexcept {
            return this->category == want && this->values.size() == 1 && this->values[0] == character;
        }

        /// @brief Tests for a single character, whatever its category.
        /// @param character The single character the token must hold.
        /// @return True when the token is exactly that one character.
        /// @complexity O(1).
        [[nodiscard]] constexpr bool is(const char character) const noexcept {
            return this->values.size() == 1 && this->values[0] == character;
        }

        /// Deleted: comparing whole tokens is almost always a mistake, because
        /// two tokens with the same text may differ in category and location.
        /// Compare `symbol`, or use is().
        [[nodiscard]] constexpr bool operator==(const Token&) const noexcept = delete;
    };

    static_assert(std::is_trivially_copyable_v<Token>, "Token must remain trivially copyable");

}