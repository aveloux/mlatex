#pragma once

#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"

namespace render::primitives {

    /// @brief The characters and marks a document cannot type directly.
    ///
    /// Two kinds of thing, for the same reason. A `%` starts a comment and a
    /// `$` starts a formula, so a document that wants to print one has to ask
    /// for it by name. And an em dash or an ellipsis has no key at all, so it
    /// has to be asked for by name too.
    ///
    /// @par Language
    /// @code
    /// 50\% of \$100        % the characters the engine reads as instructions
    /// a\_b \#4 R\&D
    /// \{braced\}           \textbackslash
    ///
    /// wait\ldots           % the marks a keyboard has no key for
    /// pages 10\textendash20
    /// a pause\textemdash like this
    /// \LaTeX{} and \TeX{}
    /// @endcode
    ///
    /// @par Why a primitive and not a macro
    /// A macro's body is read by the same lexer the document is, so a macro
    /// that tried to produce a `%` would have its body eaten as a comment
    /// before it was ever stored. These have to be bound where the lexer
    /// cannot reach them, which is here.
    ///
    /// Nothing here can fail, so the module keeps no traceback list.
    class Symbols {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Symbols(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs the named characters.
        /// @param parser  Parser to bind into.
        /// @param context Engine services; unused by this module.
        void operator()(syntax::Parser& parser, Context& context) const;
    };

}
