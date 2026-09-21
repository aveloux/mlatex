#pragma once

#include "syntax/mouth.hpp"
#include "syntax/primitives/context.hpp"
#include "syntax/traceback.hpp"

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
        /// @param names Interning table, shared with the expander.
        explicit Relay(Lexicon& names) noexcept;

        /// @brief Installs every conditional.
        /// @param mouth   Expander to bind into.
        /// @param context Engine services; numeric tests scan against its ledger.
        void operator()(Mouth& mouth, Context& context) const;

        static constexpr std::size_t limit = 1'000'000uz;   ///< Tokens one scan may read.

        /// @brief Takes a branch: keeps the text when the test held, skips it
        ///        otherwise, applying a pending `\\unless` first.
        /// @param mouth     Expander to read from when skipping.
        /// @param condition What the test decided.
        /// @complexity O(1) when taken, O(n) in the skipped tokens otherwise.
        void path(Mouth& mouth, bool condition) const;

        /// @brief Errors this module has recorded.
        [[nodiscard]] const std::vector<Traceback>& tracebacks() const noexcept { return faults; }

    private:
        /// @brief Records an error this module found.
        ///
        /// Each module keeps its own list rather than sharing one: nothing has
        /// to be constructed and passed in, and Wrapper::tracebacks() gathers
        /// them when a run finishes.
        ///
        /// @param type     What kind of mistake it is.
        /// @param location Where it happened; a zero location means "no position".
        /// @param message  Human-readable explanation.
        void fault(Traceback::Type type, memory::Location location, std::string_view message) const;

        /// @brief Reads past a branch that was not taken, to `\\else` or `\\fi`.
        /// @param mouth Expander to read from.
        void skip(Mouth& mouth) const;

        /// @brief Reads past the remainder of a branch that was taken, to `\\fi`.
        /// @param mouth Expander to read from.
        void drop(Mouth& mouth) const;

        /// @brief Are two macros the same definition, as `\\ifx` means it?
        /// @param first  Left operand.
        /// @param second Right operand.
        /// @return True when arity and body agree token for token.
        static bool compare(const Mouth::Macro& first, const Mouth::Macro& second) noexcept;

        mutable std::vector<Traceback> faults{};   ///< Errors this module found.

        Symbol finish{};        ///< `\\fi`, the close of every conditional.
        Symbol alternative{};   ///< `\\else`, the branch taken when a test fails.
        Symbol branch{};        ///< `\\or`, a case separator inside `\\ifcase`.
        Symbol integer{};       ///< `\\integer`, so a test may read a register.
        Symbol length{};        ///< `\\length`, so `\\ifdim` may read a register.
        Symbol terminate{};     ///< `\\endcsname`, the close of an `\\ifcsname` name.

        mutable bool inverted = false;   ///< Set by `\\unless`, cleared by the next test.
    };

}