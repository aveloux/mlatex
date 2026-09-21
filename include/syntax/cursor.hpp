#pragma once

#include "syntax/tokens.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace syntax {

    /// @brief The token stack the expander reads from.
    ///
    /// Held reversed internally, so the next token is `back()`. That makes
    /// inject() -- which the expander calls on every macro expansion -- O(k)
    /// in the number of tokens injected rather than O(n) in the stream.
    ///
    /// @warning Because it is a stack, the buffer ingested **last** is read
    ///          **first**.
    class Cursor {
    public:
        /// @brief An empty stream.
        Cursor() = default;

        /// @brief A stream over an already-lexed buffer.
        /// @param stream Tokens in reading order; moved from and reversed.
        explicit Cursor(std::vector<Token> stream);

        /// @brief Peeks without consuming.
        /// @param offset How far ahead, 0 for the next token.
        /// @return The token, or an empty one past the end.
        /// @complexity O(1).
        [[nodiscard]] Token lookahead(std::size_t offset = 0) const noexcept;

        /// @brief Consumes and returns the next token.
        /// @return The token, or an empty one when the stream is drained.
        /// @complexity O(1).
        Token advance() noexcept;

        /// @brief Pushes tokens onto the front of the stream.
        ///
        /// Safe to call with a span that points into this cursor's own
        /// storage; the range is copied first in that case, because insert()
        /// may reallocate partway through and leave the source dangling.
        ///
        /// @param batch Tokens in reading order.
        /// @complexity O(k) amortized.
        void inject(std::span<const Token> batch);

        /// @brief Takes ownership of a token buffer.
        ///
        /// When the stream is empty this is a move and a reverse, with no
        /// copy at all -- which is what ingesting a whole document should
        /// cost. Otherwise it falls back to inject().
        ///
        /// @param incoming Tokens in reading order; moved from.
        /// @complexity O(n) with no allocation when the stream is empty.
        void adopt(std::vector<Token>&& incoming);

        /// @brief Is the stream drained?
        /// @complexity O(1).
        [[nodiscard]] bool empty() const noexcept;

        /// @brief How many tokens are pending.
        /// @complexity O(1).
        [[nodiscard]] std::size_t size() const noexcept;

        /// @brief How many tokens have been consumed in total.
        ///
        /// Monotonic across injections, unlike a positional index, which stops
        /// meaning anything once expansion starts pushing tokens back.
        ///
        /// @complexity O(1).
        [[nodiscard]] std::size_t consumed() const noexcept;

        /// @brief Discards everything pending.
        /// @complexity O(n).
        void clear() noexcept;

        /// @brief Reserves room for pending tokens.
        /// @param capacity Tokens to make room for.
        void reserve(std::size_t capacity);

    private:
        std::vector<Token> tokens{};   ///< Held reversed, so back() is next.
        std::size_t served = 0;
    };

}