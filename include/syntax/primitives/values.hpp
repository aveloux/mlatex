#pragma once

#include "syntax/mouth.hpp"
#include "syntax/primitives/context.hpp"
#include "syntax/traceback.hpp"

#include <vector>
#include "syntax/semantics/registers.hpp"

#include <array>
#include <cstdint>
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
        /// @param lexicon Interning table, shared with the expander.
        explicit Values(Lexicon& lexicon) noexcept;

        /// @brief Installs the register primitives.
        /// @param mouth   Expander to bind into.
        /// @param context Engine services; assignments land in its registers.
        void operator()(Mouth& mouth, Context& context) const;

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

        /// The last glue read's stretch and shrink, in scaled points or
        /// scaled `fil`s, and their orders of infinity packed as the Order
        /// bank packs them: what an assignment to a glue register keeps
        /// beside its natural width.
        mutable std::array<std::int32_t, 3> flex{};

        /// @brief Reads a `<kind><index>` or a bound name.
        /// @param mouth   Expander to read from.
        /// @param context Engine services, for scanning the index.
        /// @return The addressed slot, or std::nullopt on a malformed reference.
        [[nodiscard]] std::optional<semantics::Registers::Target> slot(Mouth& mouth, Context& context) const;

        /// @brief Skips one optional noise word such as `=` or `by`.
        /// @param mouth Expander to read from.
        /// @param text  The word to skip.
        static void skip(Mouth& mouth, std::string_view text);

        Lexicon* lexicon{nullptr};   ///< For naming the banks when this module installs.

        Symbol relax{};     ///< `\\relax`, which ends an expression.
        Symbol toks{};      ///< `\\toks`, the token registers by number.
        Symbol numexpr{};   ///< `\\numexpr`, which \\the prints as a number.
        Symbol dimexpr{};   ///< `\\dimexpr`, which \\the prints as a length.

        /// The next register each bank gives out -- integers, lengths, glue
        /// -- counted up from 128 to the engine's own at 240.
        mutable std::array<std::size_t, 3> following{128, 128, 128};
        mutable std::size_t boxes{10};    ///< The next box register \\newbox gives out.
        mutable std::size_t tokens{10};   ///< The next token register \\newtoks gives out.

        mutable std::array<std::vector<Token>, semantics::Registers::slots> lists{};   ///< The token registers.
        mutable std::vector<std::uint16_t> listed{};   ///< The token register each name means, plus one; 0 for none.
    };

}