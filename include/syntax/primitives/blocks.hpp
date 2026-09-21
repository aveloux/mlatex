#pragma once

#include "syntax/mouth.hpp"
#include "syntax/primitives/context.hpp"
#include "syntax/traceback.hpp"

#include <string_view>
#include <vector>

namespace syntax::primitives {

    /// @brief Named block structure: `\\enter{name}` ... `\\leave{name}`.
    ///
    /// The document's outermost block is conventionally `\\enter{document}`.
    /// A block is a scope of kind Scope::Type::Environment, so closing one with
    /// a bare `}` is caught by Mouth::pop(), and closing it under the wrong
    /// name is caught here.
    ///
    /// @par Language
    /// @code
    /// \enter{document}
    ///     \enter{quote}
    ///         text
    ///     \leave{quote}
    /// \leave{document}
    /// @endcode
    ///
    /// Blocks nest strictly. `\\enter{a} \\enter{b} \\leave{a}` is reported at
    /// the `\\leave`, naming both the block that was open and the one the
    /// document tried to close.
    class Blocks {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param names Interning table, shared with the expander.
        explicit Blocks(Lexicon& names) noexcept;

        /// @brief Installs `\\enter` and `\\leave`.
        /// @param mouth   Expander to bind into.
        /// @param context Engine services; unused by this module.
        void operator()(Mouth& mouth, Context& context) const;

        /// @brief How many blocks are open.
        /// @complexity O(1).
        [[nodiscard]] std::size_t depth() const noexcept { return open.size(); }

        /// @brief Name of the innermost open block, or empty when none is.
        [[nodiscard]] std::string_view innermost() const noexcept;

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

        /// @brief Reads a `{name}` argument and interns it.
        /// @param mouth Expander to read from.
        /// @return The interned name, or kInvalidSymbol when the group was empty.
        [[nodiscard]] Symbol title(Mouth& mouth) const;

        mutable std::vector<Symbol> open{};   ///< Names of the blocks currently entered.
        Lexicon* names = nullptr;             ///< For resolving names back to text.
    };

}