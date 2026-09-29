#pragma once

#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"

namespace render::primitives {

    /// @brief Rules: solid rectangles of ink.
    ///
    /// A rule is the only mark the engine makes that is not a glyph. It is how
    /// a document draws an underline, a table border, or the bar in a
    /// fraction, and it takes part in layout like anything else.
    ///
    /// @par Language
    /// @code
    /// \rule{40pt}{0.4pt}   % width, then height
    /// \hrule               % a thin line across, 0.4pt high
    /// \vrule               % a thin line down, 0.4pt wide
    /// \underline{text}     % the text, with a rule the width of it beneath
    /// @endcode
    ///
    /// Nothing here can fail, so the module keeps no traceback list: a
    /// dimension that will not scan gives a rule of no size, which marks
    /// nothing and takes no room.
    class Rules {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Rules(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs the rule primitives.
        /// @param parser  Parser to bind into.
        /// @param context Engine services; dimensions scan against its registers.
        void operator()(syntax::Parser& parser, Context& context) const;
    };

}
