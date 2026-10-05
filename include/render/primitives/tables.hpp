#pragma once

#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"
#include "syntax/traceback.hpp"

#include <array>
#include <string>
#include <vector>

namespace render::primitives {

    /// @brief Aligned material: `\\halign`, and LaTeX's `tabular`.
    ///
    /// Every cell in a column is measured, the widest sets the column, and the
    /// rest are padded out to it. That is the whole of alignment, and it is
    /// why a table's columns line up even though nothing said how wide they
    /// were meant to be.
    ///
    /// @par Language
    /// @code
    /// \halign{
    ///     {one}   & {two}    \cr
    ///     {three} & {four}   \cr
    /// }
    ///
    /// \begin{tabular}{l|cr}          % flush left, a rule, centred, flush right
    ///     \toprule                   % booktabs' rules: heavy at the top and
    ///     Name & Value & Unit \\     % foot, light between, with air around
    ///     \midrule
    ///     $g$ & 9.81 & m/s$^2$ \\
    ///     \multicolumn{2}{c}{both} & \\
    ///     \bottomrule
    /// \end{tabular}
    /// @endcode
    ///
    /// `\\halign`'s cells are brace groups, `&` between them and `\\cr` ending a
    /// row. A `tabular`'s are whatever stands between its `&` and `\\\\`, each
    /// read as a group of its own, so a style changed in one cell stops at its
    /// edge. Its preamble takes `l`, `c`, `r`, `p{width}` and the rest of
    /// LaTeX's column letters; `\\hline`, `\\cline` and booktabs' four rules go
    /// between rows. `tabularx` stretches its `X` columns to fill a width.
    class Tables {
    public:
        /// @brief One column of a table's preamble.
        struct Column {
            char align{'l'};         ///< `l`, `c` or `r`; a paragraph column is `l` with a width.
            std::string width{};     ///< A paragraph column's width as written, or empty.
            bool stretch{false};     ///< True for tabularx's `X`, which shares the width left over.
            int before{0};           ///< Vertical rules to its left.
            int after{0};            ///< Vertical rules to its right.
            bool opened{true};       ///< False when `@{}` takes away the padding on its left.
            bool closed{true};       ///< False when `@{}` takes away the padding on its right.
            std::string head{};      ///< What `>{...}` puts at the start of each of its cells, as written.
        };

        /// @brief A column type `\\newcolumntype` made: how many arguments it
        ///        takes, and the preamble it stands for.
        struct Custom {
            int count{0};           ///< Its arguments, #1 to #9.
            std::string body{};     ///< The preamble it stands for; empty for none made.
        };

        /// Column types by their letter.
        using Customs = std::array<Custom, 128>;

        /// @brief Reads a preamble: `lcr`, `p{3cm}`, `|`, `@{}`, `*{3}{c}`.
        ///
        /// A declaration in `>{...}` is kept with the column after it and
        /// read at the start of each of its cells, as the array package has
        /// it: `>{\\bfseries}c`, `>{\\centering}p{2cm}`. The text of `@{...}`
        /// and `<{...}` is read past, so a preamble written for the array
        /// package reads here unchanged.
        ///
        /// @param tokens  The preamble's tokens, without its outer braces.
        /// @param customs The column types \\newcolumntype made, or none.
        /// @return One Column per column, left to right.
        /// @complexity O(n) in the preamble's length.
        [[nodiscard]] static std::vector<Column> preamble(const std::vector<syntax::Token>& tokens,
                                                          const Customs* customs = nullptr);

        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Tables(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs `\\halign` and the `tabular` blocks.
        /// @param parser  Parser to bind into.
        /// @param context Engine services; cells are shaped with its font.
        void operator()(syntax::Parser& parser, Context& context) const;

        /// @brief Errors this module has recorded.
        ///
        /// Each module keeps its own list rather than sharing one, so nothing
        /// has to be constructed and threaded through them, and
        /// Wrapper::traceback() gathers them when a run finishes. A module
        /// records an error by appending to the list where it finds it, which
        /// is why there is no reporting function to go looking for.
        [[nodiscard]] const std::vector<syntax::Traceback>& traceback() const noexcept {
            return tracebacks;
        }

    private:
        /// @brief A table opened and not yet read: what its `\\begin` said.
        struct Opening {
            std::vector<Column> columns{};   ///< Its preamble.
            std::string width{};             ///< The width it is stretched to, as written, or empty.
            char position{'c'};              ///< Where its baseline falls: t, c or b, as `[t]` says; for a
                                             ///< long table, the side it stands at: l, c or r.
            bool breakable{false};           ///< True for longtable and its kind, which a page may end between
                                             ///< any two rows of.
        };

        /// Most rows one table may have, so a malformed table is reported
        /// rather than read to the end of the document.
        static constexpr std::size_t limit = 4096;

        /// Space between one column and the next, in points. TeX gets this
        /// from the preamble; there is no preamble here, so columns are simply
        /// set apart far enough to read as columns.
        static constexpr float gutter = 12.0f;

        mutable std::vector<syntax::Traceback> tracebacks{};   ///< Errors this module found.
        mutable std::vector<Opening> openings{};                ///< Tables opened and not yet read.
        mutable Customs customs{};                              ///< The column types \\newcolumntype made.

        syntax::Symbol row{};      ///< `\\cr`, which ends a row.
        syntax::Symbol column{};   ///< `&`, which ends a cell.
    };

}
