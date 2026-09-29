#pragma once

#include "syntax/mouth.hpp"
#include "syntax/primitives/context.hpp"
#include "syntax/traceback.hpp"

#include <vector>

#include <cstddef>

namespace syntax::primitives {

    /// @brief Iteration and explicit grouping.
    ///
    /// @par Language
    /// @code
    /// \repeat[3]{ha}                    % expands to hahaha
    /// \repeat[\integer0]{.}             % the count may come from a register
    /// \repeat[\evaluate{2*4}]{-}        % or from a calculation
    ///
    /// \group \set\integer0 = 1 \ungroup % assignments inside are undone
    /// @endcode
    ///
    /// `\\repeat` is a bounded, non-recursive loop: the body is injected n
    /// times in one go rather than re-entering the expander per pass. That
    /// makes it immune to the runaway a self-referential macro would cause,
    /// and it is why there is no general `\\while` here -- an unbounded loop
    /// belongs behind the conditional module's guards, not in the expander.
    ///
    /// `\\group` and `\\ungroup` open and close a scope by hand, for when the
    /// braces that would normally do it are inconvenient.
    class Loops {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Loops(Lexicon& lexicon) noexcept;

        /// @brief Installs `\\repeat`, `\\group`, `\\ungroup` and `\\@foreach`,
        ///        pgffor's loop over a list.
        /// @param mouth   Expander to bind into.
        /// @param context Engine services; the count scans against its registers.
        void operator()(Mouth& mouth, Context& context) const;

        static constexpr std::int32_t passes = 65536;        ///< Most repetitions one \\repeat may ask for.
        static constexpr std::size_t production = 1u << 22;  ///< Most tokens one \\repeat may produce.

        /// @brief Errors this module has recorded.
        ///
        /// Each module keeps its own list rather than sharing one, so nothing
        /// has to be constructed and threaded through them, and
        /// Wrapper::traceback() gathers them when a run finishes. A module
        /// records an error by appending to the list where it finds it, which
        /// is why there is no reporting function to go looking for.
        [[nodiscard]] const std::vector<Traceback>& traceback() const noexcept { return tracebacks; }

    private:
        mutable std::vector<Traceback> tracebacks{};   ///< Errors this module found.

        Symbol integer{};   ///< `\\integer`, so the count may be a register.
    };

}