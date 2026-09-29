#pragma once

#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"
#include "syntax/traceback.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace render::primitives {

    /// @brief Floats and their captions: `figure`, `table`, `algorithm`,
    ///        `\\caption` and `\\captionof`.
    ///
    /// @par Language
    /// @code
    /// \begin{figure}[htbp]                 % placement read, and set here
    ///     \centering
    ///     \includegraphics[width=.6\linewidth]{plot.png}
    ///     \caption{Error against resolution.}\label{fig:error}   % Figure 1: ...
    /// \end{figure}
    ///
    /// \begin{table}[t]
    ///     \centering
    ///     \caption{Convergence.}\label{tab:rates}                  % Table 1: ...
    ///     \begin{tabular}{rc} ... \end{tabular}
    /// \end{table}
    ///
    /// \begin{algorithm}                    % ruled, as the algorithm package's are
    ///     \caption{Projection step}        % Algorithm 1 Projection step
    ///     \begin{algorithmic}[1] ... \end{algorithmic}
    /// \end{algorithm}
    ///
    /// \captionof{figure}{Outside a float}  % the caption package's
    /// @endcode
    ///
    /// A float is set where it is written when it fits there, with the space
    /// LaTeX keeps around one set in the text, and otherwise at the head of
    /// the next page while the text after it carries on filling this one --
    /// LaTeX's `h` and `t`, floats of a kind kept in the order written. It
    /// is never split between pages. `[H]`, the float package's, stays where
    /// it is written whatever it leaves at the foot of the page. `\\centering` inside it
    /// ends with it. A caption is numbered by the float's own counter and
    /// printed through its `\\the` macro and its name macro --
    /// `\\figurename`, `\\tablename`, `\\algorithmname` -- so a document
    /// renames either as it would in LaTeX. A caption that fits on one line
    /// is centred and a longer one is justified, as the standard classes set
    /// them; the caption package's `labelfont`, `font` and `labelsep`
    /// options, kept as its variables, change the label's weight, the
    /// caption's size and the separator.
    class Floats {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Floats(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs the float blocks, `\\caption` and `\\captionof`.
        /// @param parser  Parser to bind into.
        /// @param context Engine services; numbers come from its counters.
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
        /// @brief One float being read.
        struct Open {
            std::string kind{};   ///< Its counter: `figure`, `table` or `algorithm`.
            bool ruled{false};    ///< Set between rules, its caption at the head, as an algorithm is.
            bool captioned{false};   ///< Its own caption has been read, and its counter stepped.
            std::uint8_t place{0};   ///< Where it may go, as layout::Node::Directive::Place bits.
            bool across{false};      ///< Starred: across the columns of a page of several.
        };

        /// @brief Reads one caption and sets it.
        /// @param parser  Parser to read the caption's text with.
        /// @param context Engine services.
        /// @param kind    What it captions: the counter it numbers by.
        /// @param ruled   True in a ruled float: the label bold, the caption flush left, a rule under it.
        /// @return The caption, as the nodes of a paragraph of its own.
        [[nodiscard]] syntax::Node* caption(syntax::Parser& parser, Context& context, const std::string& kind,
                                            bool ruled) const;

        mutable std::vector<syntax::Traceback> tracebacks{};   ///< Errors this module found.
        mutable std::vector<Open> floats{};                     ///< The floats being read, innermost last.

        /// The two marks put around a float's blocks, which a document
        /// cannot write: each name has a colon in it, where the lexer ends a
        /// control word. `hold` opens a float -- `fixed` the one for `[H]`
        /// -- and `release` closes it.
        syntax::Symbol hold{};
        syntax::Symbol fixed{};
        syntax::Symbol release{};
    };

}
