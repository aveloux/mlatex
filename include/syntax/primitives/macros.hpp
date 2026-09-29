#pragma once

#include "syntax/mouth.hpp"
#include "syntax/primitives/context.hpp"
#include "syntax/traceback.hpp"

#include <vector>

namespace syntax::primitives {

    /// @brief Definition primitives: `\\define`, `\\declare`, `\\switch`,
    ///        `\\forget`, `\\alias`, the names built and expanded out of order
    ///        -- `\\csname`, `\\expandafter` -- and the tests a command makes
    ///        of what follows it: `\\ifstar`, `\\ifnextchar`.
    ///
    /// @par Language
    /// @code
    /// \define\greeting{hello}                 % no parameters
    /// \define\twice[1]{#1#1}                  % one parameter, used as #1
    /// \define\pair[2]{#1 and #2}
    /// \forget\greeting                        % undefine
    /// \alias\hi\greeting                      % second name for one meaning
    /// \alias\hi=\greeting                     % the same, as TeX may write it
    ///
    /// \shared\define\counter{0}               % survives the enclosing block
    /// \spanning\define\body[1]{#1}            % argument may contain \par
    /// \guarded\define\fragile{...}            % may not appear in an argument
    /// \provided\define\maybe{...}             % only if \maybe means nothing yet
    /// \expanded\define\now{\greeting}         % the body expanded here: hello
    ///
    /// \define\field#1={#2},{#1 is #2}         % #1 up to '=', #2 one brace group
    /// \define\pair#1,#2;{#1 then #2}          % #1 up to ',', #2 up to ';'
    ///
    /// \define{\greet}[2][Hello]{#1, #2!}      % LaTeX's form: the name braced,
    /// \greet{world}                           % and #1 optional -- Hello, world!
    /// \greet[Goodbye]{world}                  % Goodbye, world!
    ///
    /// \declare\span{s O{1} m}{...}            % xparse: a star, [1], a group
    /// \switch\ifdraft                         % \ifdraft, \drafttrue, \draftfalse
    ///
    /// \expandafter\define\csname item3\endcsname{third}
    ///
    /// \define\name{\ifstar{starred}{plain}}   % \name* reads starred, \name plain
    /// \ifnextchar{[}{an option}{none}         % whether a [ comes next
    /// @endcode
    ///
    /// The `{\\greet}` form is LaTeX's `\\newcommand` exactly -- which the core
    /// package names as another spelling of `\\define` -- so a preamble's own
    /// commands, optional first arguments included, read as they were
    /// written. `\\declare` is xparse's `\\NewDocumentCommand` the same way:
    /// `m`, `o`, `O{default}`, `s`, `t<token>` and `+`, with an `o` left
    /// out reading as `-NoValue-` and an `s` or `t` as `\\BooleanTrue` or
    /// `\\BooleanFalse`, which is what `\\IfNoValueTF` and `\\IfBooleanTF`
    /// look for.
    ///
    /// The prefixes may be combined and in any order; each applies to the
    /// next definition only. `\\expanded` before a brace group instead is
    /// e-TeX's: the group, expanded through and read again.
    ///
    /// `[n]` declares `#1` through `#n` positionally and undelimited, the way
    /// LaTeX's own `\newcommand` does: each reads exactly one brace group.
    /// Writing the parameters out as `#1#2...` instead is plain TeX's own
    /// spelling, and unlocks what LaTeX's cannot: whatever stands after one
    /// `#N` and before the next -- or before the body's own `{` -- becomes
    /// that parameter's delimiter, read up to but not including it, brace
    /// nesting respected. Left empty, as `#1#2` is, a parameter reads one
    /// brace group exactly as `[n]` would have. A literal `#` in a body is
    /// written `##`, as the expander expects.
    class Macros {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Macros(Lexicon& lexicon) noexcept;

        /// @brief Installs the definition primitives.
        /// @param mouth   Expander to bind into.
        /// @param context Engine services; the register bank is used to scan `[n]`.
        void operator()(Mouth& mouth, Context& context) const;

        static constexpr int parameters = 9;   ///< Most parameters a definition may declare.

        /// @brief Errors this module has recorded.
        ///
        /// Each module keeps its own list rather than sharing one, so nothing
        /// has to be constructed and threaded through them, and
        /// Wrapper::tracebacks() gathers them when a run finishes. A module
        /// records an error by appending to the list where it finds it, which
        /// is why there is no reporting function to go looking for.
        [[nodiscard]] const std::vector<Traceback>& tracebacks() const noexcept { return tracebacks_; }

    private:
        mutable std::vector<Traceback> tracebacks_{};   ///< Errors this module found.

        Symbol integer{};     ///< Interned `\\integer`, so `[\integer0]` scans.
        Symbol alias{};       ///< `\\@alias`, which a switch's two macros are written with.
        Symbol truth{};       ///< `\\iftrue`, what a switch turned on means.
        Symbol falsehood{};   ///< `\\iffalse`, what a switch turned off means.
        Symbol terminate{};   ///< `\\endcsname`, the end of a built name.
        Symbol relax{};       ///< `\\relax`, what a built name means until it is defined.
        Symbol finish{};      ///< The end of a body being expanded; no document can write it.

        /// How many bodies are being expanded for an \\edef now: while one
        /// is, \\noexpand and \\unexpanded hold names back rather than let
        /// them mean \\relax.
        mutable int flattening{0};
    };

}
