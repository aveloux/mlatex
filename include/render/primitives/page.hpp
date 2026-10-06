#pragma once

#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"
#include "syntax/traceback.hpp"

#include <vector>

namespace render::primitives {

    /// @brief The page: its class, its sheet and margins, and what it carries
    ///        besides its column -- a head, a foot and its number.
    ///
    /// @par Language
    /// @code
    /// \documentclass{article}     % sets the sheet and all four margins
    ///
    /// \pagewidth    = 595pt       % then override whatever the class chose
    /// \pageheight   = 842pt
    /// \leftmargin   = 90pt
    /// \rightmargin  = 54pt
    /// \topmargin    = 72pt
    /// \bottommargin = 72pt
    /// \leading      = 14pt        % baseline to baseline within a paragraph
    /// @endcode
    ///
    /// The `=` is optional, as it is throughout the language. Every dimension
    /// accepts the usual units and reads a register where one is written, so
    /// `\\pagewidth = \\length0` does what it looks like.
    ///
    /// @par Heads, feet and numbers
    /// @code
    /// \pagestyle{plain}               % the number, centred in the foot: the default
    /// \pagestyle{empty}               % nothing
    /// \pagestyle{headings}            % the number at the right of the head
    /// \pagestyle{fancy}               % whatever \fancyhead and \fancyfoot say
    /// \thispagestyle{empty}           % this page alone
    ///
    /// \pagenumbering{roman}           % i, ii, iii from this page, counting from 1
    /// \pagenumbering{arabic}          % 1, 2, 3 from this page, counting from 1 again
    ///
    /// \fancyhead[L]{\textit{Draft}}   % L, C and R are the three places;
    /// \fancyhead[R]{Acme Ltd.}         % none given means all three
    /// \fancyfoot[C]{Page \thepage}
    /// \fancyhf{}                       % every place of both, emptied
    /// \pagerules{0.4pt}{0pt}           % the rule under the head, and over the foot
    /// @endcode
    ///
    /// A style and a numbering take effect on the page they land on, since
    /// that is only known once the pages are: each travels in the column as
    /// a block of no height, and the composer meets it as it draws its page.
    /// `\\thepage` is the number of the page it is drawn on, whether in a
    /// head, a foot or the text itself.
    ///
    /// @par Classes
    /// A class is a starting point, not a constraint: it sets the sheet and
    /// the margins, and anything after it wins. `article` and `letter` are
    /// even-margined letter paper; `book` and `report` give the binding edge
    /// more room; `a4paper` changes the sheet and leaves the margins alone.
    ///
    /// @par Frames
    /// beamer's slides, each a page of its own, its title at its head and
    /// what it holds in the middle of the rest -- at its top for `[t]`:
    /// @code
    /// \begin{frame}[t]{Title}{Subtitle} ... \end{frame}
    /// \begin{frame} \frametitle{Title} ... \end{frame}
    /// @endcode
    class Page {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Page(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs the geometry primitives.
        /// @param parser  Parser whose expander to bind into.
        /// @param context Engine services; the geometry lands in its document.
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
        mutable std::vector<syntax::Traceback> tracebacks{};   ///< Errors this module found.
        mutable std::vector<std::size_t> spread{};   ///< The columns in force, each change on top of the last.
        mutable std::vector<char> frames{};          ///< beamer's frames open: where each sets what it holds, `t`, `c` or `b`.
        mutable int slide{1};                        ///< Which slide of its frame is being set, from 1.
        mutable int pauses{1};                       ///< beamer's `beamerpauses`: the slide what follows a `\\pause` shows from.
    };

}
