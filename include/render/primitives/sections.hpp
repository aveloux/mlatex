#pragma once

#include "render/primitives/context.hpp"
#include "render/primitives/styles.hpp"
#include "syntax/parser.hpp"
#include "syntax/traceback.hpp"

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace render::primitives {

    /// @brief Headings, and the numbering that runs through them.
    ///
    /// @par Language
    /// @code
    /// \chapter{Beginnings}            % 1
    /// \section{Introduction}          % 1.1
    /// \subsection{Background}         % 1.1.1
    /// \subsubsection{Earlier work}    % 1.1.1.1
    /// \section{Method}                % 1.2      -- and the level below resets
    /// \chapter{Results}               % 2         -- as do all levels below
    /// \section*{Acknowledgements}     % no number, and the count stands
    /// \section{The case $n = 1$}      % a title is text like any other
    ///
    /// \part{Foundations}              % Part I -- its own count, roman-numbered
    ///
    /// \appendix                       % from here, the outermost level used
    /// \chapter{Proofs}                % so far numbers with a letter -- A
    /// \section{Lemmas}                % A.1
    /// @endcode
    ///
    /// Opening a level resets every level beneath it, which is what makes the
    /// second chapter's first section 2.1 rather than 1.3. A heading is set
    /// bold, in the family the body is in, at LaTeX's sizes for its level,
    /// with its number a quad before its title. The number is what a
    /// `\\label` after the heading refers to.
    ///
    /// Each level is a counter of the Counters module's, printed through its
    /// `\\the` macro -- so `\\setcounter{section}{4}` and a redefined
    /// `\\thesection` both mean what LaTeX means by them, and a theorem
    /// numbered within sections restarts with each one. A section is
    /// numbered within its chapter once a document has used one.
    ///
    /// `\\part` stands outside the chain: it keeps its own roman-numbered
    /// count and resets nothing beneath it, exactly as LaTeX's does.
    class Sections {
    public:
        /// How many levels of heading there are.
        static constexpr std::size_t levels = 4;

        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Sections(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs the heading primitives.
        /// @param parser  Parser to bind into.
        /// @param context Engine services; headings are shaped with its font.
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
        /// @brief How one level's heading looks, once a class or titlesec's
        ///        `\\titleformat` has said: its face and size, where it
        ///        stands, how its number is written and how far from its
        ///        title, and the space around it.
        struct Look {
            bool set{false};                          ///< Said at all; the standard classes' look otherwise.
            std::vector<Styles::Cut> cuts{};          ///< The face, as \\bfseries and the rest change it, from \\normalfont.
            Styles::Size size{Styles::Size::Normal};  ///< Its size.
            char align{'l'};                          ///< `l`, `c` or `r`: flush left, centred, flush right.
            bool upper{false};                        ///< Its title in capitals: `\\MakeUppercase`.
            bool display{false};                      ///< Its number on a line of its own above the title.
            bool unlabelled{false};                   ///< No number shown, numbered or not.
            std::string label{};                      ///< How its number is written, as \\Roman{section} and a full
                                                      ///< stop write it; empty for the counter's own.
            float sep{-1.0f};                         ///< Between number and title, in points; below zero for a quad.
            std::pair<float, float> spacing{-1.0f, -1.0f};   ///< Above and below, in points; below zero for the class's.
            std::string color{};                      ///< Its color, as `\\color{...}` names it; empty for the text's.
            float rule{0.0f};                         ///< A rule under it this thick, titlesec's \\titlerule; none at 0.
        };

        mutable std::vector<syntax::Traceback> tracebacks{};   ///< Errors this module found.

        /// Each level's look, chapter to subsubsection.
        mutable std::array<Look, levels> looks{};

        /// True once a `\\chapter` has been set: from then on a section is
        /// numbered within its chapter, 2.1 rather than 1, as a book's are.
        mutable bool chaptered = false;

        /// True once `\\appendix` has been read: a chapter from then on is an
        /// Appendix rather than a Chapter.
        mutable bool appendix = false;

        /// Where each list stands -- 0 the contents, 1 the figures, 2 the
        /// tables, 3 the index -- to be filled as the document ends.
        mutable std::vector<std::pair<std::size_t, syntax::Node*>> places{};
        std::array<syntax::Symbol, 4> marks{};     ///< The marks that keep their places; no document can write them.

        /// @brief One `\\index` entry, as makeindex reads one.
        struct Term {
            std::vector<std::string> keys{};                     ///< What it sorts by, a level each: `a!b`.
            std::vector<memory::Slice<syntax::Node*>> shown{};   ///< What it prints, a level each: `key@shown`.
            std::string encap{};                                 ///< After its `|`: `textbf`, `see{...}`, `(` or `)`.
            std::size_t anchor{0};                               ///< Where it stands, for its page.
        };
        mutable std::vector<Term> terms{};   ///< Every \\index entry, in the order written.
    };

}
