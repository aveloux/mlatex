#pragma once

#include "syntax/expression/unicodes.hpp"
#include "memory/slice.hpp"

#include <cstdint>
#include <string_view>

namespace syntax::expression {

    /// @brief One node of a formula's tree.
    ///
    /// A formula is parsed into a tree before anything is measured, because
    /// how a piece of it is set depends on what surrounds it: an exponent is
    /// smaller than its base, a fraction's parts are smaller than the
    /// fraction, and a parenthesis grows to whatever it encloses. The tree is
    /// what lets the typesetter see all of that at once.
    ///
    /// One struct serves every kind, and the kind says which fields mean
    /// anything. That keeps a node one allocation and the whole tree a handful
    /// of pointers, where a hierarchy of node classes would cost a virtual
    /// call per visit for no gain the typesetter could use.
    ///
    /// @par Example
    /// `\\frac{a+1}{b}` parses to a Fraction whose #left is a Binary `+` over
    /// the Variables `a` and `1`, and whose #right is the Variable `b`.
    struct Node {
        /// @brief Whether the formula sits in a line or on a line of its own.
        enum class Style : std::uint8_t {
            Inline,   ///< In the run of the text: `$...$` and `\\(...\\)`.
            Display   ///< On a line of its own: `\\[...\\]` and `$$...$$`.
        };

        /// @brief What a node is, which says which of its fields mean anything.
        enum class Type : std::uint8_t {
            Variable,   ///< A symbol, letter or digit: uses #codepoint or #value.
            Binary,     ///< An infix operator: #left, #right, and the operator in #value.
            Unary,      ///< A prefix or postfix operator: #left, and the operator in #value.
            Group,      ///< A sub-formula in #left, fenced by #open and #close when they are set.
            Script,     ///< A base in #left with a #subscript, a #superscript, or both.
            Fraction,   ///< #left over #right; #value names the kind, as `\\binom` does.
            Radical,    ///< A root of #right, with an optional degree in #left.
            Accent,     ///< A mark over #left; #value names which mark.
            Sequence,   ///< A run of #arguments, or a named command with them.
            Matrix,     ///< A grid of #arguments, #columns wide, fenced by #open and #close.
            Text        ///< Words set as written, spaces and all, in #value: what `\\text` holds.
        };

        /// @brief How a Matrix spaces its columns; unused otherwise.
        enum class Grid : std::uint8_t {
            Spaced,     ///< A quad between every two columns, each cell in text style:
                        ///< the matrices, `cases` and `array`.
            Displayed,  ///< The same, each cell a displayed line: `gathered`, `dcases`.
            Paired      ///< Columns in right-and-left pairs, touching within a pair and two
                        ///< quads apart between pairs, each cell a displayed line:
                        ///< `aligned`, and the `align` displays.
        };

        Type type = Type::Variable;                                    ///< What the node is.
        Unicodes::Category category = Unicodes::Category::Ordinary;    ///< Its class, for spacing.
        Style style = Style::Inline;                                   ///< Inline or displayed.
        std::uint32_t codepoint = 0;                                   ///< The character, when known.

        /// @brief Its text as written.
        ///
        /// A Matrix keeps its preamble here instead: one letter per column,
        /// `l`, `c` or `r`, read again from the start when the grid is wider
        /// than it -- `c` for a matrix, `ll` for `cases`, `rl` for `aligned`.
        std::string_view value{};

        /// @brief The character on each side of a group, or 0 for none.
        ///
        /// Code points rather than bytes, because a fence may be an angle
        /// bracket or a floor as easily as a parenthesis. A brace group has
        /// neither, which is how it groups without printing anything.
        std::uint32_t open = 0;
        std::uint32_t close = 0;   ///< @copydoc open

        Node* left = nullptr;                ///< First operand, or the only one.
        Node* right = nullptr;               ///< Second operand.
        Node* subscript = nullptr;           ///< What sits below and after.
        Node* superscript = nullptr;         ///< What sits above and after.
        memory::Slice<Node*> arguments{};    ///< Every argument, in order.

        /// @brief How many columns wide a Matrix is; unused otherwise.
        ///
        /// #arguments holds every cell in row-major order -- the first
        /// #columns entries are the first row, and so on -- rather than a
        /// node per row, since a row is not a formula in its own right and
        /// nothing else ever needs to address one on its own.
        std::size_t columns = 0;

        Grid grid = Grid::Spaced;   ///< How a Matrix spaces its columns and sets its cells.
    };

}
