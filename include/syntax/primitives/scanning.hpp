#pragma once

#include "syntax/mouth.hpp"

#include <span>

namespace syntax::primitives {

    /// @brief Forces expansion so that a computed value can stand where a
    ///        number is written.
    ///
    /// Number scans the token stream directly and does not expand, so
    /// `\\set\\integer0 = \\evaluate{6*7}` would otherwise see the unexpanded
    /// `\\evaluate` and fail. Calling this first runs whatever is pending until
    /// an unexpandable token surfaces, then puts that token back, leaving the
    /// digits it produced at the front of the stream.
    ///
    /// Register names such as `\\integer` are neither macros nor primitives, so
    /// they are left alone and scan as Number intends. Leading blanks are
    /// consumed, which a number context would discard in any case.
    ///
    /// @param mouth Expander whose stream should be settled.
    /// @param limit Most expansions to force before giving up.
    /// @complexity O(1) when the next token is already unexpandable, which is
    ///             the common case.
    inline void prime(Mouth& mouth, const int limit = 64) {
        for (int guard = 0; guard < limit; ++guard) {
            // Leading blanks first: a number context ignores them, and
            // stopping on one would leave `= \evaluate{...}` unexpanded.
            while (mouth.lookahead().category == CatCodes::Category::Space) {
                mouth.read();
            }

            const Token next = mouth.lookahead();
            if (next.category != CatCodes::Category::Escape) return;
            if (mouth.lookup(next.symbol) == nullptr && mouth.primitive(next.symbol) == nullptr) {
                return;
            }

            const Token produced = mouth.expand();
            if (produced.empty()) return;

            mouth.inject(std::span{&produced, 1});

            // Expanding yielded another expandable control sequence with the
            // same name: nothing is advancing, so stop rather than spin.
            if (produced.symbol == next.symbol) return;
        }
    }

}