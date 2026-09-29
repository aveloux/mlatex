#pragma once

#include <cstdint>
#include <string_view>

namespace syntax::semantics {

    /// @brief What kind of open group a scope is, for diagnostics.
    ///
    /// Scope used to be a stack of its own, opened and closed alongside
    /// Mouth's undo log -- which kept an identical stack, since it is Mouth
    /// that has to know a group's kind to catch `\\begin{a} ... }` as a
    /// mismatch. Two stacks that must always agree are one stack too many, so
    /// only Mouth's remains; this is what is left of Scope, a tag for the kind
    /// a caller hands to Mouth::push() and Mouth::pop().
    class Scope {
    public:
        /// @brief Which kind of group is open.
        enum class Type : std::uint8_t {
            Group,       ///< A brace group or `\\begingroup ... \\endgroup`.
            Environment, ///< `\\begin{name} ... \\end{name}`.
            Equations,   ///< A formula: `$...$`, `$$...$$`, `\\(...\\)`, `\\[...\\]`.
            Box,         ///< `\\hbox`, `\\vbox`, `\\vtop`.
            Conditional, ///< `\\if`, `\\ifx`, `\\else`, `\\fi`.
            Alignment    ///< `\\halign`, `\\valign`, a table.
        };

        /// @brief One name per Type, for a traceback message.
        /// @param type Kind to name.
        /// @return Its name, lower case, as it reads in a message.
        /// @complexity O(1).
        [[nodiscard]] static std::string_view name(Type type) noexcept;
    };

}
