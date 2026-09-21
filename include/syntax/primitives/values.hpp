#pragma once

#include "syntax/mouth.hpp"
#include "syntax/primitives/context.hpp"
#include "syntax/traceback.hpp"

#include <vector>
#include "syntax/semantics/registers.hpp"

#include <optional>

namespace syntax::primitives {

    /// @brief Register primitives: typed storage and arithmetic on it.
    ///
    /// @par Language
    /// @code
    /// \set\integer0 = 42             % an integer register
    /// \set\length1 = 1.5pt           % a dimension
    /// \set\glue2 = 3pt               % flexible space
    ///
    /// \set\integer1 = \integer0      % registers read as values
    /// \set\length0 = \integer0 pt    % an integer used as a factor
    ///
    /// \increase\integer0 by 5
    /// \reduce\integer0 by 2
    /// \scale\integer0 by 3
    ///
    /// \name\tally\integer0           % \tally now addresses that slot
    /// \set\tally = 7
    /// @endcode
    ///
    /// The `=` and `by` are optional noise words, accepted for readability and
    /// skipped when present. There are 256 slots per kind, numbered 0 to 255.
    ///
    /// `\\name` binds a control sequence to a slot, which is what makes a
    /// register readable inside any later number: once bound, `\\tally` scans
    /// as a value wherever an integer is expected.
    class Values {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param names Interning table, shared with the expander.
        explicit Values(Lexicon& names) noexcept;

        /// @brief Installs the register primitives.
        /// @param mouth   Expander to bind into.
        /// @param context Engine services; assignments land in its ledger.
        void operator()(Mouth& mouth, Context& context) const;

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

        /// @brief What kind of register a control sequence names.
        /// @param symbol Interned control sequence.
        /// @return Its bank, or std::nullopt when it names no bank.
        [[nodiscard]] std::optional<semantics::Registers::Type> bank(Symbol symbol) const noexcept;

        /// @brief Reads a `<kind><index>` or a bound name.
        /// @param mouth   Expander to read from.
        /// @param context Engine services, for scanning the index.
        /// @return The addressed slot, or std::nullopt on a malformed reference.
        [[nodiscard]] std::optional<semantics::Registers::Target> slot(Mouth& mouth, Context& context) const;

        /// @brief Skips one optional noise word such as `=` or `by`.
        /// @param mouth Expander to read from.
        /// @param text  The word to skip.
        static void skip(Mouth& mouth, std::string_view text);

        Symbol integer{};   ///< `\\integer`, the count bank.
        Symbol length{};    ///< `\\length`, the dimension bank.
        Symbol glue{};      ///< `\\glue`, the flexible-space bank.
    };

}