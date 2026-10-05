#pragma once

#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"

namespace render::primitives {

    /// @brief Penalties: what a break at this point would cost.
    ///
    /// The line breaker weighs every possible break against every other, and a
    /// penalty is how a document puts its thumb on that scale. A positive one
    /// discourages a break, a negative one invites it, and ten thousand either
    /// way is absolute.
    ///
    /// @par Language
    /// @code
    /// \penalty 500      % a break here would be unwelcome
    /// \nobreak          % and here, out of the question
    ///
    /// \break            % break the line here, whatever it costs
    /// \linebreak        % the same thing said the other way
    /// first line \\     % LaTeX's way, which sets the line before
    /// second line       % it at its natural width
    /// \newline          % the same as \\, spelled out
    /// \pagebreak        % and end the page here
    /// \newpage          % LaTeX's name for the same thing
    /// \clearpage        % and TeX's, for anyone who wants it
    /// \@breakable{https://example.com/a/b}   % a line may end after any /
    /// @endcode
    ///
    /// Nothing here can fail, so the module keeps no traceback list: a number
    /// that will not scan is taken as zero, which is a break neither
    /// encouraged nor discouraged.
    class Penalties {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Penalties(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs the penalty primitives.
        /// @param parser  Parser to bind into.
        /// @param context Engine services; the count scans against its registers.
        void operator()(syntax::Parser& parser, Context& context) const;

        /// The cost at which a break becomes forbidden, or forced when negated.
        static constexpr std::int32_t absolute = 10000;
    };

}
