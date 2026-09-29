#pragma once

#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"
#include "syntax/traceback.hpp"

#include <string>
#include <vector>

namespace render::primitives {

    /// @brief Text set as it was typed: `\\verb`, the `verbatim` block, and
    ///        the listings package's `lstlisting`, `\\lstinline` and
    ///        `\\lstinputlisting`.
    ///
    /// @par Language
    /// @code
    /// Call \verb|parse(x_1, $y$)| first, or \lstinline{main()}.
    ///
    /// \begin{verbatim}
    ///   for (int i = 0; i < n; ++i) {}   % not a comment, \not a command
    /// \end{verbatim}
    ///
    /// \lstset{basicstyle=\small, numbers=left}
    /// \begin{lstlisting}[caption={A loop}, label=lst:loop, frame=single]
    /// def f(x):
    ///     return x
    /// \end{lstlisting}
    /// @endcode
    ///
    /// The lexer reads the text itself, before any of it could mean anything
    /// -- which is what TeX does by making every character an ordinary one --
    /// and hands it over as one token after the command or the block's
    /// opening. Here it is set in the typewriter face, a line to a line,
    /// every space as wide as a character and no wider: an inline piece as
    /// one box a line may not break inside, a block as one box per line, so
    /// a page may end between any two.
    ///
    /// A listing's options are its own or \\lstset's: `numbers=left` numbers
    /// its lines in the margin, `caption` and `label` caption it "Listing 1"
    /// above, `frame` rules it above and below, `basicstyle` sets its size,
    /// and `tabsize` how many columns a tab reaches to. A `comment` block,
    /// the verbatim package's, is read and set as nothing.
    class Verbatim {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Verbatim(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs the primitives.
        /// @param parser  Parser to bind into.
        /// @param context Engine services; the text is shaped with its fonts.
        void operator()(syntax::Parser& parser, Context& context) const;

        /// @brief Errors this module has recorded.
        [[nodiscard]] const std::vector<syntax::Traceback>& tracebacks() const noexcept { return tracebacks_; }

    private:
        /// @brief A block opened and not yet read: what it is, and its options.
        struct Opening {
            std::string kind;      ///< `verbatim`, `lstlisting` or `comment`.
            std::string options;   ///< As written between its brackets.
            bool headed{false};    ///< Its caption, if it has one, has been read.
        };

        mutable std::vector<syntax::Traceback> tracebacks_{};   ///< Errors this module found.
        mutable std::vector<Opening> openings{};                ///< Blocks opened, innermost last.

        syntax::Symbol start{};   ///< The mark a block's opening leaves; no document can write it.
    };

}
