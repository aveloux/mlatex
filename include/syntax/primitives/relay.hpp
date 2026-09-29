#pragma once

#include "syntax/mouth.hpp"
#include "syntax/primitives/context.hpp"
#include "syntax/traceback.hpp"

#include <array>
#include <vector>

#include <cstddef>

namespace syntax::primitives {

    /// @brief Conditionals: the `\\if` family, `\\else`, `\\or`, `\\fi`.
    ///
    /// @par Language
    /// @code
    /// \iftrue yes\else no\fi
    /// \unless\ifdefined\name it is missing\fi
    /// \ifnum\integer0 > 10 big\else small\fi
    /// \ifdim\length1 < 2pt narrow\fi
    /// \ifcase\integer0 zero\or one\or two\else many\fi
    /// \ifx\first\second same\fi
    /// \ifempty{#1} nothing was passed\fi
    /// \ifmmode\alpha\else$\alpha$\fi % a formula either way
    /// @endcode
    ///
    /// A false branch is not parsed, only counted past: skip() reads tokens
    /// and tracks nesting until the matching `\\else` or `\\fi`, so the text
    /// that is not taken never reaches the parser and never has to be valid.
    ///
    /// @par Bounds
    /// Every scan stops after #limit tokens. A document missing an `\\fi`
    /// therefore reports one error instead of swallowing the rest of itself,
    /// which is the difference between a diagnosable file and a hang.
    ///
    /// @par Use
    /// Context holds a reference to this module, so build it before the
    /// Context that names it. Wrapper already does that.
    class Relay {
    public:
        /// @brief Interns the control sequences this module compares against.
        /// @param lexicon Interning table, shared with the expander.
        explicit Relay(Lexicon& lexicon) noexcept;

        /// @brief Installs every conditional.
        /// @param mouth   Expander to bind into.
        /// @param context Engine services; numeric tests scan against its registers.
        void operator()(Mouth& mouth, Context& context) const;

        static constexpr std::size_t limit = 1'000'000uz;   ///< Tokens one scan may read.

        /// @brief Takes a branch: keeps the text when the test held, skips it
        ///        otherwise, applying a pending `\\unless` first.
        /// @param mouth     Expander to read from when skipping.
        /// @param condition What the test decided.
        /// @complexity O(1) when taken, O(n) in the skipped tokens otherwise.
        void path(Mouth& mouth, bool condition) const;

        /// @brief Errors this module has recorded.
        ///
        /// Each module keeps its own list rather than sharing one, so nothing
        /// has to be constructed and threaded through them, and
        /// Wrapper::tracebacks() gathers them when a run finishes. A module
        /// records an error by appending to the list where it finds it, which
        /// is why there is no reporting function to go looking for.
        [[nodiscard]] const std::vector<Traceback>& tracebacks() const noexcept { return tracebacks_; }

    private:
        /// @brief Reads past a branch that was not taken, to `\\else` or `\\fi`.
        /// @param mouth Expander to read from.
        void skip(Mouth& mouth) const;

        /// @brief Reads past the remainder of a branch that was taken, to `\\fi`.
        /// @param mouth Expander to read from.
        void drop(Mouth& mouth) const;

        mutable std::vector<Traceback> tracebacks_{};   ///< Errors this module found.

        Symbol finish{};        ///< `\\fi`, the close of every conditional.
        Symbol alternative{};   ///< `\\else`, the branch taken when a test fails.
        Symbol branch{};        ///< `\\or`, a case separator inside `\\ifcase`.
        Symbol integer{};       ///< `\\integer`, so a test may read a register.
        Symbol length{};        ///< `\\length`, so `\\ifdim` may read a register.
        Symbol terminate{};     ///< `\\endcsname`, the close of an `\\ifcsname` name.

        /// The tests named `\\if...` that take their branches as arguments
        /// -- `\\ifstar`, `\\ifnextchar`, `\\ifstrequal`, `\\ifvariable`,
        /// `\\ifpackageloaded`, `\\iffile`, each under its `@` name as well --
        /// and so have no `\\fi` for a skip to count.
        std::array<Symbol, 12> forms{};

        /// @brief Whether a token opens a conditional a skip has to count
        ///        past: a primitive `\\if...` closed by `\\fi`, or another
        ///        name for one. A macro is never one, whatever it is called,
        ///        because a skipped branch is not expanded.
        /// @param mouth Expander that knows what the token means.
        /// @param token Token to look at.
        /// @return True for a conditional's opening.
        /// @complexity O(1).
        [[nodiscard]] bool opens(const Mouth& mouth, const Token& token) const noexcept;

        mutable bool inverted = false;   ///< Set by `\\unless`, cleared by the next test.
    };

}