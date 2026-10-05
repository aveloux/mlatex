#pragma once

#include <string>

namespace render::primitives {

    /// @brief Formats a counter's value the way LaTeX's numbering commands do.
    ///
    /// `\arabic`, `\roman`/`\Roman` and `\alph`/`\Alph` all reduce to the same
    /// shape: a small positive integer in, a run of characters out. Kept in
    /// one place so a heading's own numbering -- `\part`, `\appendix`'s
    /// letters -- and the counter primitives format identically.
    class Numeral {
    public:
        /// @brief Plain decimal, as `\arabic` writes it.
        [[nodiscard]] static std::string arabic(int value);

        /// @brief Roman numerals, as `\roman` (lower) or `\Roman` (upper) writes it.
        /// @param value Value to render; 0 or negative yields an empty string, as TeX does.
        /// @param upper True for `\Roman`, false for `\roman`.
        [[nodiscard]] static std::string roman(int value, bool upper);

        /// @brief A single letter, as `\alph` (lower) or `\Alph` (upper) writes it.
        /// @param value Clamped to 1-26, mapping onto a-z or A-Z.
        /// @param upper True for `\Alph`, false for `\alph`.
        [[nodiscard]] static std::string alphabetic(int value, bool upper);

        /// @brief A number in English words, as fmtcount writes it:
        ///        `twenty-three`, `one hundred and five`, or as an ordinal,
        ///        `twenty-third`.
        /// @param value   Any whole number; a negative one is `minus` it.
        /// @param ordinal True for the ordinal, `first` rather than `one`.
        /// @complexity O(log n) in the value.
        [[nodiscard]] static std::string words(int value, bool ordinal);
    };

}
