#pragma once

#include "syntax/cursor.hpp"
#include "syntax/mouth.hpp"
#include "syntax/primitives/context.hpp"
#include "syntax/traceback.hpp"

#include <vector>

#include <cstdint>
#include <optional>

namespace syntax::primitives {

    /// @brief Arithmetic: `\\evaluate` and the comparison it enables.
    ///
    /// This is what lets the language calculate on its own rather than only
    /// storing numbers. `\\evaluate` expands to its result as digits, so it
    /// composes anywhere a number is written.
    ///
    /// @par Language
    /// @code
    /// \evaluate{2 + 3 * 4}              % expands to 14
    /// \evaluate{(2 + 3) * 4}            % expands to 20
    /// \evaluate{-7 / 2}                 % expands to -3, truncating toward zero
    /// \evaluate{17 \mod 5}              % expands to 2
    /// \evaluate{2 ^ 10}                 % expands to 1024
    ///
    /// \set\integer0 = \evaluate{6 * 7}  % composes with \set
    /// \evaluate{\integer0 + 1}          % registers read as operands
    /// \evaluate{"FF + '10}              % hex and octal operands
    /// @endcode
    ///
    /// @par Grammar
    /// @code
    /// expression := term (('+' | '-') term)*
    /// term       := power (('*' | '/' | '\mod') power)*
    /// power      := factor ('^' power)?
    /// factor     := '(' expression ')' | <number>
    /// @endcode
    ///
    /// `^` binds tighter than `*` and associates to the right, as in most
    /// languages. `<number>` is whatever Number::integer accepts, so signs,
    /// octal, hex, character codes and register references all work.
    ///
    /// Remainder is `\mod`, not `%`: `%` opens a comment at lexing time, so
    /// it would swallow the rest of the line before this module ever saw it.
    ///
    /// Operands are scanned, not expanded, so a nested `\evaluate` inside the
    /// braces does not run. Use a register for an intermediate result.
    ///
    /// Arithmetic runs in std::int64_t and is clamped to the 32-bit range a
    /// register holds; overflow, division by zero and malformed input are
    /// reported and yield 0 rather than propagating nonsense.
    class Compute {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param names Interning table, shared with the expander.
        explicit Compute(Lexicon& names) noexcept;

        /// @brief Installs `\\evaluate`.
        /// @param mouth   Expander to bind into.
        /// @param context Engine services; operands resolve against its ledger.
        void operator()(Mouth& mouth, Context& context) const;

        static constexpr std::int64_t ceiling = 2147483647;   ///< Widest value a register holds.
        static constexpr std::size_t depth = 64;              ///< Deepest parenthesis nesting.

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

        mutable std::vector<Traceback> faults{};   ///< Errors this module found.

        /// @brief State threaded through one evaluation.
        struct Pass {
            Cursor& cursor;                     ///< Tokens of the expression.
            const semantics::Registers& ledger; ///< Resolves register operands.
            Symbol integer;                     ///< Interned `\\integer`.
            const Compute& owner;               ///< Records errors on this module.
            Symbol modulo;                      ///< Interned `\\mod`.
            std::size_t level = 0;              ///< Current parenthesis depth.
            bool sound = true;                  ///< Cleared by malformed input; result becomes 0.
            bool noted = false;                 ///< An overflow has already been reported.
        };

        [[nodiscard]] static std::int64_t expression(Pass& pass);
        [[nodiscard]] static std::int64_t term(Pass& pass);
        [[nodiscard]] static std::int64_t power(Pass& pass);
        [[nodiscard]] static std::int64_t factor(Pass& pass);

        /// @brief Skips spaces and returns the next token without consuming it.
        [[nodiscard]] static Token peek(Pass& pass);

        /// @brief Clamps to the register range, reporting once if it had to.
        [[nodiscard]] static std::int64_t bound(Pass& pass, std::int64_t value);

        Symbol integer{};   ///< `\\integer`, so register operands scan.
        Symbol modulo{};    ///< `\\mod`, the remainder operator.
    };

}