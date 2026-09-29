#pragma once

#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"
#include "syntax/traceback.hpp"

#include <vector>

namespace render::primitives {

    /// @brief Pseudocode: the `algorithmic` block and the lines inside it.
    ///
    /// @par Language
    /// @code
    /// \begin{algorithmic}[1]          % every line numbered; [5], every fifth
    ///     \State $x \gets 0$          % a numbered line, at the block's depth
    ///     \Statex                     % a line with no number
    ///     \algorithmopen              % the lines after this one sit a step in
    ///     \algorithmclose             % and back out again
    /// \end{algorithmic}
    /// @endcode
    ///
    /// Only the lines are the engine's own. The words -- `\\For`, `\\If`,
    /// `\\EndWhile`, `\\Require` -- are the algpseudocode package's, written
    /// over these as that package writes them over algorithmicx, each block
    /// a line that opens a step in and a line that closes it. So a document
    /// that redefines `\\algorithmicrequire` to read `Input:` gets that.
    ///
    /// A line is a paragraph of its own. Its number, in `\\footnotesize`,
    /// stands right-aligned at the block's left edge, where every line's
    /// number lines up, and its text starts a step in for each block it is
    /// inside -- as does every line the text wraps onto.
    class Algorithms {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Algorithms(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs the `algorithmic` block, `\\State`, `\\Statex` and
        ///        the two depth primitives.
        /// @param parser  Parser to bind into.
        /// @param context Engine services.
        void operator()(syntax::Parser& parser, Context& context) const;

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
        /// @brief One `algorithmic` block being read.
        struct Block {
            int every{0};    ///< Number every this many lines; 0 numbers none.
            int lines{0};    ///< How many numbered lines it has had.
            int depth{0};    ///< How many steps in its lines sit.
        };

        /// @brief One line: the paragraph it opens, its margin, and its number.
        /// @param parser   Parser, for the arena and the location.
        /// @param context  Engine services.
        /// @param numbered True for a `\\State`, false for a `\\Statex`.
        /// @return The nodes that open the line.
        [[nodiscard]] syntax::Node* line(syntax::Parser& parser, Context& context, bool numbered) const;

        /// How far each block steps its lines in, in ems: algorithmicx's
        /// `\\algorithmicindent`.
        static constexpr float indent = 1.5f;

        mutable std::vector<syntax::Traceback> tracebacks_{};   ///< Errors this module found.
        mutable std::vector<Block> blocks{};                    ///< The blocks being read, innermost last.
    };

}
