#pragma once

#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"
#include "syntax/traceback.hpp"

#include <array>
#include <string>
#include <unordered_set>
#include <vector>

namespace render::primitives {

    /// @brief Explicit boxes: TeX's `\\hbox`, `\\vbox` and `\\vtop`, and
    ///        LaTeX's over them.
    ///
    /// A box gathers whatever is written inside it and measures the lot as one
    /// thing, which is how a document keeps a run of material together when
    /// everything around it is being broken apart.
    ///
    /// @par Language
    /// @code
    /// \hbox{kept on one line}
    /// \hbox to 200pt{stretched across two hundred points}
    /// \vbox{stacked
    ///       downwards}
    ///
    /// \mbox{kept together}                  \makebox[3cm][r]{flush right}
    /// \fbox{framed}                         \framebox[3cm]{framed, centred}
    /// \colorbox{yellow}{on yellow}          \fcolorbox{red}{white}{both}
    /// \raisebox{1ex}{lifted}                \makebox[0pt][l]{overlapping}
    /// \phantom{room}  \hphantom{across}  \vphantom{upright}
    ///
    /// \parbox[t]{4cm}{paragraphs four centimetres wide}
    /// \begin{minipage}{0.45\textwidth} ... \end{minipage}
    /// \begin{subfigure}{0.45\textwidth} ... \caption{Left} \end{subfigure}
    /// \begin{columns} \column{0.5\textwidth} left \column{0.5\textwidth} right \end{columns}
    /// \begin{block}{Title} ... \end{block}      alertblock, exampleblock
    ///
    /// \newsavebox{\stash} \sbox{\stash}{kept} \usebox{\stash}
    /// \settowidth{\gap}{widest label}
    /// @endcode
    ///
    /// `to` is accepted on a horizontal box and sets its width, stretching or
    /// shrinking the glue inside to reach it. Without it a box takes whatever
    /// width its contents come to. A frame leaves `\\fboxsep` of room and is
    /// `\\fboxrule` thick, both registers a document may set. A column --
    /// `\\parbox`, `minipage` -- sets `\\linewidth` to its own width inside,
    /// opens its paragraphs flush, and hangs on the line by its middle, or by
    /// its first or last baseline for `[t]` or `[b]`.
    class Boxes {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Boxes(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs the box primitives.
        /// @param parser  Parser to bind into.
        /// @param context Engine services; contents are shaped with its font.
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
        /// @brief A minipage opened and not yet read: how it hangs, and how wide.
        struct Opening {
            char position;   ///< `c`, `t` or `b`.
            float width;     ///< In points.
        };

        mutable std::vector<syntax::Traceback> tracebacks{};   ///< Errors this module found.
        mutable std::vector<Opening> openings{};                ///< Minipages opened, innermost last.
        mutable std::array<layout::Node*, syntax::semantics::Registers::slots> stored{};   ///< The box registers.

        /// `\\hbox`, `\\vbox`, `\\vtop` and `\\mbox`: what may follow `\\setbox`'s number.
        std::array<syntax::Symbol, 4> made{};

        /// @brief A framed block opened and not yet read: which, and its options.
        struct Frame {
            std::string kind;      ///< `framed`, `shaded`, `leftbar`, `mdframed` or `tcolorbox`.
            std::string options;   ///< As written between its brackets.
            float width;           ///< The column inside, in points.
            float rule;            ///< The frame's thickness; none for a shaded block.
            float room;            ///< Between the frame and the column.
        };
        mutable std::vector<Frame> frames{};   ///< Framed blocks opened, innermost last.
        mutable std::string preset{};          ///< tcolorbox's options for every box, as \\tcbset gave them.
        mutable std::unordered_set<std::string> boxed{};   ///< The boxes \\newtcolorbox has named.

        /// @brief A beamer columns block open: where its columns hang unless
        ///        one says otherwise, and whether a column `\\column` started
        ///        is still open, its scope with it.
        struct Spread {
            char position;   ///< `c`, `t` or `b`.
            bool open;       ///< A `\\column` is being read.
        };
        mutable std::vector<Spread> spreads{};   ///< Columns blocks opened, innermost last.

        syntax::Symbol framing{};  ///< The mark a framed block's opening leaves.
        syntax::Symbol framed{};   ///< The mark its end leaves.

        syntax::Symbol start{};    ///< The mark a minipage's opening leaves; no document can write it.
        syntax::Symbol finish{};   ///< The mark its end leaves.
    };

}
