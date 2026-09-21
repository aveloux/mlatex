#pragma once

#include "syntax/mouth.hpp"
#include "syntax/primitives/context.hpp"
#include "syntax/traceback.hpp"

#include <vector>

namespace syntax::primitives {

    /// @brief Definition primitives: `\\define`, `\\forget`, `\\alias`.
    ///
    /// @par Language
    /// @code
    /// \define\greeting{hello}                 % no parameters
    /// \define\twice[1]{#1#1}                  % one parameter, used as #1
    /// \define\pair[2]{#1 and #2}
    /// \forget\greeting                        % undefine
    /// \alias\hi\greeting                      % second name for one meaning
    ///
    /// \shared\define\counter{0}               % survives the enclosing block
    /// \spanning\define\body[1]{#1}            % argument may contain \par
    /// \guarded\define\fragile{...}            % may not appear in an argument
    /// @endcode
    ///
    /// The three prefixes may be combined and in any order; each applies to
    /// the next definition only.
    ///
    /// Parameters are positional and undelimited: `[n]` declares `#1` through
    /// `#n`. A literal `#` in a body is written `##`, as the expander expects.
    class Macros {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param names Interning table, shared with the expander.
        explicit Macros(Lexicon& names) noexcept;

        /// @brief Installs the definition primitives.
        /// @param mouth   Expander to bind into.
        /// @param context Engine services; the register ledger is used to scan `[n]`.
        void operator()(Mouth& mouth, Context& context) const;

        static constexpr int parameters = 9;   ///< Most parameters a definition may declare.

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

        Symbol integer{};   ///< Interned `\\integer`, so `[\integer0]` scans.
    };

}