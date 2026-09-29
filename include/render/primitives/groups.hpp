#pragma once

#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"

namespace render::primitives {

    /// @brief Braces, and everything they hold together.
    ///
    /// A brace group does two things at once, and both matter. It keeps its
    /// contents together for whatever reads them next, and it bounds every
    /// change made inside it -- a font, a register, a macro -- so that the
    /// change lasts exactly as long as the braces do.
    ///
    /// @par Language
    /// @code
    /// {\bfseries bold from here}  and back to normal
    /// {\large only this is large}
    /// \begingroup \itshape a group by name \endgroup
    /// \bgroup \bfseries a brace written as a name \egroup
    /// @endcode
    ///
    /// Without this the braces would reach the page as two printed characters
    /// and the style would run to the end of the document, which are the two
    /// ways a grouping construct can fail.
    ///
    /// @par Why the face is stamped
    /// The text inside a group is not shaped where it is read; it is shaped
    /// when the document is composed, long after the group has closed and the
    /// style it chose has been put back. So on the way out, every piece of
    /// text the group holds that has not already been claimed by an inner
    /// group is stamped with the face that was in use inside it.
    ///
    /// Nothing here can fail, so the module keeps no traceback list: an
    /// unclosed brace is the parser's to report, and it does.
    class Groups {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Groups(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs the group primitive.
        /// @param parser  Parser to bind into.
        /// @param context Engine services; the face comes from its selection.
        void operator()(syntax::Parser& parser, Context& context) const;

    private:
        mutable std::vector<Selection> kept{};   ///< What each open block was entered with.
        syntax::Symbol close{};   ///< `\\egroup`, which closes a brace group as `}` does.
        syntax::Symbol end{};     ///< `\\endgroup`, which closes `\\begingroup`'s.
    };

}
