#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace syntax::expression {

    /// @brief Maths symbol names to the characters they stand for: `alpha`
    ///        to U+03B1, `le` to U+2264, `sum` to U+2211.
    ///
    /// A perfect hash built from `assets/unicodes.gperf` when the engine is
    /// compiled, so a name is found in one probe and nothing is read at run
    /// time. Each symbol also carries its category -- whether it is an
    /// operator, a relation, an opening delimiter -- because that is what
    /// decides the space around it in a formula.
    class Unicodes {
    public:
        /// @brief What kind of atom a symbol makes, as TeX classes them.
        enum class Category : std::uint8_t {
            Ordinary,    ///< Standard math symbols, variables, and digits (e.g., x, 1)
            Operator,    ///< Prefix operators and functions (e.g., \\sum, \\sin)
            Binary,      ///< Binary operations (e.g., +, \\times)
            Relation,    ///< Comparison operators (e.g., =, \\le)
            Opening,     ///< Left delimiters (e.g., (, [)
            Closing,     ///< Right delimiters (e.g., ), ])
            Punctuation, ///< Punctuation marks (e.g., ,, ;)
            Inner,       ///< Enclosed structures like fractions
            Accent       ///< Math diacritics (e.g., \\hat, \\vec)
        };

        /// @brief One symbol: its character, and the atom it makes.
        struct Symbol {
            std::uint32_t codepoint;   ///< The character.
            Category category;         ///< The kind of atom.
        };

        /// The replacement character, for a symbol that has none of its own.
        static const std::uint32_t invalid;

        /// @brief The table; there is nothing to set up.
        Unicodes();

        /// @brief Finds a symbol by its name, without the backslash.
        /// @param name As `alpha` or `le`.
        /// @return The symbol, or nothing when the name is not one.
        /// @complexity O(n) in the name's length, one probe.
        [[nodiscard]] std::optional<Symbol> get(std::string_view name) const noexcept;
    };

}
