#pragma once

#include "render/primitives/context.hpp"
#include "memory/dictionary.hpp"
#include "syntax/parser.hpp"
#include "syntax/traceback.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace render::primitives {

    /// @brief Theorems and proofs, as amsthm has them.
    ///
    /// @par Language
    /// @code
    /// \theoremstyle{plain}                            % bold head, italic body
    /// \newtheorem{theorem}{Theorem}[section]          % 2.1, 2.2, ... reset each section
    /// \newtheorem{lemma}[theorem]{Lemma}              % counted with the theorems
    /// \theoremstyle{definition}                       % bold head, upright body
    /// \newtheorem{definition}[theorem]{Definition}
    /// \theoremstyle{remark}                           % italic head, upright body
    /// \newtheorem*{remark}{Remark}                    % never numbered
    ///
    /// \begin{theorem}[Energy inequality]\label{thm:energy}
    ///     ...                                         % Theorem 2.1 (Energy inequality).
    /// \end{theorem}
    ///
    /// \begin{proof}                                   % Proof. ... and a box at the margin
    ///     ...
    /// \end{proof}
    /// \begin{proof}[Proof of \cref{thm:energy}]       % the head named otherwise
    /// @endcode
    ///
    /// Every environment `\\newtheorem` declares is watched here and set
    /// natively: its head, its number from the counter it names -- through
    /// that counter's `\\the` macro, so `\\numberwithin` and a redefined
    /// `\\thetheorem` both reach it -- and its body in the style it was
    /// declared under. A `\\label` inside refers to its number, and
    /// `\\cref` names it by its title. Declaring one again replaces it.
    ///
    /// The end of a proof is marked with `\\qed`, which pushes
    /// `\\qedsymbol` -- the package's to define, and a document's to
    /// redefine -- out to the right margin of the proof's last line.
    class Theorems {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Theorems(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs `\\newtheorem`, `\\theoremstyle`, the proof block and `\\qed`.
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
        /// @brief amsthm's three styles.
        enum class Style : std::uint8_t {
            Plain,        ///< Bold head, italic body: theorems, lemmas, corollaries.
            Definition,   ///< Bold head, upright body: definitions, examples.
            Remark        ///< Italic head, upright body: remarks, notes.
        };

        /// @brief One environment `\\newtheorem` declared.
        struct Kind {
            std::string title{};     ///< What its head calls it: `Theorem`.
            std::string counter{};   ///< The counter it numbers by, or empty for none.
            Style style{Style::Plain};   ///< How it is set.
        };

        mutable std::vector<syntax::Traceback> tracebacks{};    ///< Errors this module found.
        mutable memory::Dictionary<Kind> kinds{};                ///< Every environment declared, by name.
        mutable memory::Dictionary<Style> styles{};              ///< Styles by name, amsthm's and a document's.
        mutable Style style{Style::Plain};                       ///< The style the next declaration takes.
    };

}
