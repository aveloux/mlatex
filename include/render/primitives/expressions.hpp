#pragma once

#include "render/primitives/context.hpp"
#include "syntax/expression/parser.hpp"
#include "syntax/parser.hpp"
#include "syntax/traceback.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace render::primitives {

    /// @brief Mathematics: where an expression starts, and what is an operator.
    ///
    /// @par Language
    /// @code
    /// $x + 1$              % inline, in the run of the text
    /// \(x + 1\)            % the same thing, unambiguously delimited
    ///
    /// $$x + 1$$            % displayed, on a line of its own and centred
    /// \[x + 1\]            % likewise
    ///
    /// \begin{equation}     % displayed and numbered, the number flush
    ///     E = mc^2         % with the right margin: (1), (2), ...
    ///     \label{energy}   % and \ref{energy} is that number
    /// \end{equation}
    ///
    /// \begin{equation*}    % displayed, without a number
    ///     E = mc^2
    /// \end{equation*}
    ///
    /// \pmatrix{a & b \\ c & d}     % a grid, fenced in parentheses
    /// \bmatrix{a & b \\ c & d}     % the same, in square brackets
    /// \vmatrix{a & b \\ c & d}     % and between two bars, as a determinant
    /// @endcode
    ///
    /// Inside, the expression parser (syntax::expression::Parser) takes over.
    /// It knows the symbol names from the maths tables; this module tells it
    /// what binds tighter than what, which is what makes `a + b * c` group
    /// the way it reads.
    ///
    /// @par Precedence
    /// Four levels, loosest first: relations, then addition, then
    /// multiplication, then the postfix factorial. Structures -- fractions,
    /// roots, accents, matrices -- take brace arguments instead of operands
    /// and so sit outside the ordering entirely.
    class Expressions {
    public:
        /// @brief Interns the control sequences this module binds, and
        ///        builds the grammar every formula is read by.
        /// @param lexicon Interning table, shared with the expander.
        explicit Expressions(syntax::Lexicon& lexicon);

        /// @brief Installs the expression delimiters.
        /// @param parser  Parser to bind into.
        /// @param context Engine services; symbol names come from its tables.
        void operator()(syntax::Parser& parser, Context& context) const;

        /// @brief The grammar every formula is read by: built once, here,
        ///        rather than again for each formula.
        [[nodiscard]] const syntax::expression::Grammar& grammar() const noexcept { return grammar_; }

        /// @brief Errors this module has recorded.
        ///
        /// Each module keeps its own list rather than sharing one, so nothing
        /// has to be constructed and threaded through them, and
        /// Wrapper::tracebacks() gathers them when a run finishes. A module
        /// records an error by appending to the list where it finds it, which
        /// is why there is no reporting function to go looking for.
        [[nodiscard]] const std::vector<syntax::Traceback>& tracebacks() const noexcept {
            return tracebacks_;
        }

    private:
        /// @brief How the display being opened sets its lines.
        enum class Layout : std::uint8_t {
            Single,    ///< One formula: `\\[`, `equation`, and every inline formula.
            Rows,      ///< A grid whose rows are lines of their own: `align`, `gather`.
            Multline   ///< One formula broken over lines: `multline`.
        };

        /// @brief One row of a display that numbers its rows.
        struct Row {
            std::string number{};   ///< Its equation number as printed, or empty for none.
            std::string tag{};      ///< What `\\tag` printed in its place, or empty.
        };

        /// @brief Gives the display being opened the next equation number,
        ///        and makes it what a `\\label` refers to.
        /// @param mouth   Expander, which prints the number.
        /// @param context Engine services, for the counter.
        void count(syntax::Mouth& mouth, Context& context) const;

        /// @brief Takes back the number just given out, for a line that
        ///        turned out to carry none: `\\nonumber`, `\\tag`.
        /// @param context Engine services, for the counter.
        static void retract(const Context& context);

        /// @brief An equation's number, or a tag, as the box it prints as.
        ///
        /// Set upright in the text face whatever style the text around it is
        /// in, as LaTeX sets it -- an equation in a theorem is not numbered in
        /// the theorem's italic.
        ///
        /// @param context Engine services, for the text face.
        /// @param arena   Allocator for the box.
        /// @param text    What to print: `(3)`, or a tag.
        /// @return The box, or nullptr for no text or no face.
        [[nodiscard]] static layout::Node* label(Context& context, memory::Arena& arena, std::string_view text);

        /// @brief One displayed line: a formula centred in the column, and its
        ///        number, if it has one, flush with the right edge.
        /// @param arena   Allocator for the line.
        /// @param body    The formula's box.
        /// @param mark    Its number, or nullptr for none.
        /// @param balance Room kept on each side for the widest number among
        ///                the lines set together, so they all centre alike.
        /// @param column  Width of the column.
        /// @return The line.
        [[nodiscard]] static layout::Node* line(memory::Arena& arena, layout::Node* body, layout::Node* mark,
                                                float balance, float column);

        /// @brief Parses one expression and returns it as a node.
        /// @param parser    Parser to read from.
        /// @param context   Engine services.
        /// @param display   True for an expression on a line of its own.
        /// @param delimiter Character that ends it, or 0 when a name does.
        /// @param stop      Control sequence that ends it, or none.
        /// @return The expression, as a node.
        [[nodiscard]] syntax::Node* enter(
            syntax::Parser& parser,
            Context& context,
            bool display,
            char delimiter,
            syntax::Symbol stop
        ) const;

        mutable std::vector<syntax::Traceback> tracebacks_{};   ///< Errors this module found.
        syntax::expression::Grammar grammar_;                  ///< What every formula is read by.

        /// The number the display being read will carry, as printed, or empty
        /// for none. Set when an `equation` block opens rather than when its
        /// expression ends, so a `\\label` inside it already refers to it.
        mutable std::string numbered{};

        mutable Layout setting{Layout::Single};  ///< How the next display sets its lines.
        mutable bool counting{false};            ///< True when that display numbers them.
        mutable std::vector<Row> lines{};        ///< The rows of the display being read.
        mutable std::string tagged{};            ///< A `\\tag` for the display being read, or empty.
    };

}
