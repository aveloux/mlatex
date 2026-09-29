#pragma once

#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"
#include "syntax/traceback.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace render::primitives {

    /// @brief Diagrams the packages for them write: tikz-cd's, xy-pic's and
    ///        amscd's commutative diagrams, quantikz's and Qcircuit's
    ///        circuits, and forest's, qtree's and tikz-qtree's trees.
    ///
    /// @par Language
    /// @code
    /// \[\begin{tikzcd}
    ///     A \arrow[r, "f"] \arrow[d, "g"'] & B \arrow[d, hook] \\
    ///     C \arrow[r, "h"', dashed] & D
    /// \end{tikzcd}\]
    ///
    /// \[\xymatrix{A \ar[r]^f \ar[d]_g & B \ar[d]^h \\ C \ar[r]_k & D}\]
    ///
    /// \[\begin{CD} A @>f>> B \\ @VgVV @VVhV \\ C @>>k> D \end{CD}\]
    ///
    /// \begin{quantikz}
    ///     \lstick{$\ket{0}$} & \gate{H} & \ctrl{1} & \meter{} \\
    ///     \lstick{$\ket{0}$} & \qw      & \targ{}  & \qw
    /// \end{quantikz}
    ///
    /// \begin{forest} [S [NP [the] [cat]] [VP [sat]]] \end{forest}
    /// \Tree [.S [.NP the cat ] [.VP sat ] ]
    /// @endcode
    ///
    /// Each is read whole and written out as the TikZ picture it draws: a
    /// `\\matrix` of its objects, each column as wide as its widest and each
    /// row as high as its highest, with each package's own spacing, and a
    /// `\\draw` for each arrow between the objects it joins -- its tips, its
    /// dashes, a double line, a bend or a shift, and its labels beside it on
    /// the side the package puts them, in script size. A circuit's wires run
    /// along its rows, its gates boxed on them and its controls joined to
    /// their targets; a tree's nodes stand level under level, each parent
    /// centred over its children, each child joined to it.
    ///
    /// A display holding one of them alone is set as the picture it is --
    /// see Expressions -- and one inside a formula of other things as the
    /// grid of its objects, its arrows let go.
    ///
    /// @par Reported
    /// An arrow to a cell the diagram does not have, as a warning; the
    /// diagram is drawn without it.
    class Diagrams {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Diagrams(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs the diagrams' blocks and commands.
        /// @param parser  Parser to bind into.
        /// @param context Engine services; its arena keeps what is written out.
        void operator()(syntax::Parser& parser, Context& context) const;

        /// @brief Errors this module has recorded.
        [[nodiscard]] const std::vector<syntax::Traceback>& traceback() const noexcept { return tracebacks; }

    private:
        /// @brief One arrow: the cells it joins, counted from 1, how it is
        ///        drawn, and its labels, each a TikZ node on it.
        struct Arrow {
            std::size_t row{0};                   ///< The row it leaves.
            std::size_t column{0};                ///< The column it leaves.
            std::size_t down{0};                  ///< The row it reaches.
            std::size_t across{0};                ///< The column it reaches.
            std::string style{};                  ///< The `\\draw` options.
            std::vector<std::string> labels{};    ///< `node[...] {...}`, each.
        };

        /// @brief A grid of objects, formulas each, and the arrows between them.
        struct Diagram {
            std::vector<std::vector<std::string>> cells{};   ///< Row by row; an empty one holds nothing.
            std::vector<Arrow> arrows{};                     ///< In the order written.
            std::string columns{"2.4em"};                    ///< Between two columns' edges.
            std::string rows{"1.8em"};                       ///< Between two rows' edges.
            std::string nodes{};                             ///< Options every object's node takes.
        };

        /// @brief The TikZ picture a diagram is.
        /// @param diagram The diagram.
        /// @param origin  Where it was written, for a warning.
        /// @return Its source: a matrix and a `\\draw` for each arrow.
        /// @complexity O(n) in its cells and arrows.
        [[nodiscard]] std::string picture(const Diagram& diagram, memory::Location origin) const;

        /// @brief What a block holds, as written, up to the `\\end` that
        ///        closes it -- which is left to be read, so the block closes
        ///        as any does.
        /// @param mouth Expander to read from.
        /// @param name  The block.
        /// @complexity O(n) in its tokens.
        [[nodiscard]] static std::string body(syntax::Mouth& mouth, std::string_view name);

        /// @brief A grid's text cut into its rows, at each `\\\\`, and each
        ///        row into its cells at the separator, neither inside braces.
        /// @param text      The grid.
        /// @param separator What stands between two cells: `&`, or what
        ///                  `ampersand replacement` names.
        /// @complexity O(n) in the text.
        [[nodiscard]] static std::vector<std::vector<std::string>> grid(std::string_view text,
                                                                        std::string_view separator);

        /// @brief One node of a tree: its label and its children.
        struct Branch {
            std::string label{};              ///< Its text.
            std::vector<Branch> children{};   ///< Left to right.
            float width{0.0f};                ///< How wide its subtree stands, in points.
            float x{0.0f};                    ///< Its centre across, in points.
        };

        /// @brief Reads one bracketed node of a tree and everything under it:
        ///        forest's `[label, options [child] ...]`, or qtree's
        ///        `[.label child words ]`, a bare word a leaf.
        /// @param text The tree, from where it stands.
        /// @param at   Where to read; left past the node.
        /// @return The node.
        /// @complexity O(n) in the text read.
        [[nodiscard]] static Branch branch(std::string_view text, std::size_t& at);

        /// @brief Places a tree's nodes, each parent centred over its
        ///        children, and writes each node and each edge to it.
        /// @param tree  The subtree, its #Branch::width already worked out.
        /// @param left  Where its room starts across, in points.
        /// @param depth How many levels down it is.
        /// @param count The nodes written so far, for their names; grown.
        /// @param out   The picture's source; appended to.
        /// @return The name of its root's node.
        static std::string plant(Branch& tree, float left, int depth, int& count, std::string& out);

        mutable std::vector<syntax::Traceback> tracebacks{};   ///< Errors this module found.
    };

}
