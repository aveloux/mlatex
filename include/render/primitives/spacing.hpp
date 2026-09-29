#pragma once

#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"

namespace render::primitives {

    /// @brief Space: glue that may flex, and kerns that may not.
    ///
    /// The difference is the whole of it. Glue is where a line may be broken
    /// and where the slack is taken up when it is set to a width; a kern is a
    /// fixed distance that neither stretches nor permits a break.
    ///
    /// @par Language
    /// @code
    /// \hskip 10pt      % flexible space across
    /// \vskip 10pt      % flexible space down
    /// \kern 3pt        % fixed, and no break here
    ///
    /// \quad \qquad     % one em and two, as kerns
    ///
    /// \hfil \hfill     % stretch to fill; the longer name wins over the shorter
    /// \vfil \vfill
    ///
    /// \smallskip \medskip \bigskip   % the three fixed vertical amounts
    ///
    /// a~b              % a space the line may not break at
    /// @endcode
    ///
    /// `~` is TeX's tie: the same width the current face gives an ordinary
    /// space, but a kern rather than glue, so "Mr.~Smith" cannot end a line
    /// between the two.
    ///
    /// `\\hfil` and `\\hfill` stretch without limit, and a `\\hfill` in a line
    /// takes all the slack that a `\\hfil` beside it would otherwise have had.
    /// That is how a line is pushed to one side or centred: put infinite glue
    /// where the space should go.
    ///
    /// Nothing here can fail, so the module keeps no traceback list. A
    /// dimension that will not scan leaves a zero-width space, which is the
    /// same thing the document asked for written differently.
    class Spacing {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Spacing(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs the spacing primitives.
        /// @param parser  Parser to bind into.
        /// @param context Engine services; dimensions scan against its registers.
        void operator()(syntax::Parser& parser, Context& context) const;
    };

}
