#pragma once

#include "syntax/mouth.hpp"
#include "syntax/primitives/context.hpp"
#include "syntax/traceback.hpp"

#include <vector>

namespace syntax::primitives {

    /// @brief Text taken apart and tested: xstring's commands, under its
    ///        own names.
    ///
    /// A template built from a program's values asks of a value what TeX's
    /// own primitives cannot easily say -- whether it holds a word, what
    /// stands before a dash in it, how long it is. xstring answers by
    /// expanding TeX macros a character at a time; here each is one native
    /// command, worked out on the expanded text in a single pass.
    ///
    /// @par Language
    /// @code
    /// \IfSubStr{\variable{plan}}{gold}{Welcome back.}{Upgrade today.}
    /// \IfBeginWith{ISBN 978}{ISBN}{a book}{}  \IfEndWith{report.pdf}{.pdf}{a PDF}{}
    /// \IfStrEq{a}{a}{same}{different}         \IfEq{1.0}{1}{equal}{}      % as numbers
    /// \IfInteger{42}{whole}{}                 \IfDecimal{3.5}{a number}{}
    /// \StrLen{hello}                          % 5
    /// \StrLeft{hello}{2} \StrRight{hello}{2}  % he, lo
    /// \StrMid{hello}{2}{4} \StrChar{hello}{1} % ell, h
    /// \StrGobbleLeft{hello}{1}                % ello
    /// \StrBefore{2026-09-29}{-}               % 2026
    /// \StrBehind[2]{2026-09-29}{-}            % 29: after the second dash
    /// \StrBetween{<a>}{<}{>}                  % a
    /// \StrSubstitute{a.b.c}{.}{,}             % a,b,c
    /// \StrDel{a-b}{-} \StrCount{banana}{a}    % ab, 3
    /// \StrPosition{banana}{n}                 % 3
    /// \StrLeft{hello}{2}[\start]              % nothing set: \start is `he`
    /// @endcode
    ///
    /// Every argument is expanded before it is read, as xstring's are by
    /// default; a starred test reads them as written, which here comes to
    /// the same text. Characters are UTF-8's, each counted whole, so `é` is
    /// one. A function's result is set where it stands, or -- given a
    /// macro's name in brackets after its arguments -- kept in that macro.
    class Strings {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Strings(Lexicon& lexicon) noexcept;

        /// @brief Installs the tests -- `\\IfSubStr`, `\\IfBeginWith`,
        ///        `\\IfEndWith`, `\\IfStrEq`, `\\IfEq`, `\\IfInteger`,
        ///        `\\IfDecimal` -- and the functions -- `\\StrLen`,
        ///        `\\StrLeft`, `\\StrRight`, `\\StrMid`, `\\StrChar`,
        ///        `\\StrGobbleLeft`, `\\StrGobbleRight`, `\\StrBefore`,
        ///        `\\StrBehind`, `\\StrBetween`, `\\StrSubstitute`, `\\StrDel`,
        ///        `\\StrCount`, `\\StrPosition`.
        /// @param mouth   Expander to bind into.
        /// @param context Engine services; unused by this module.
        void operator()(Mouth& mouth, Context& context) const;

        /// @brief Every error this module has recorded: a count that is no
        ///        number. Wrapper::traceback() gathers them when a run finishes.
        [[nodiscard]] const std::vector<Traceback>& traceback() const noexcept { return tracebacks; }

    private:
        mutable std::vector<Traceback> tracebacks{};   ///< Errors this module found.
    };

}
