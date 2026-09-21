#pragma once

#include "memory/location.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace syntax {

    /// @brief One recorded error, with the source position that caused it.
    ///
    /// Collected rather than thrown: TeX reports and carries on, so a single
    /// mistake costs one command rather than the rest of the document.
    class Traceback {
    public:
        /// @brief What kind of mistake this is.
        enum class Type : std::uint8_t {
            Group,       ///< Mismatched or unclosed curly brace scope group
            Equation,    ///< Invalid or unclosed text formula boundary
            Environment, ///< Unmatched \\begin and \\end environment block
            Delimiter,   ///< Unbalanced \\left and \\right delimiter pair
            Argument,    ///< Missing or malformed macro argument
            Token,       ///< Unexpected or invalid token encountered
            End,         ///< Premature end of file reached
            Macro,       ///< Undefined or invalid macro command
            Recursion,   ///< Infinite macro expansion recursion detected
            Memory,      ///< Memory arena capacity limit exceeded
            Catcode,     ///< Invalid character category code assignment
            Scope,       ///< Unmatched scope exit operation
            Primitive,   ///< Failure executing underlying compiler primitive
            Dimension,   ///< Invalid or unparseable unit dimension specification
            Register,    ///< Out-of-bounds register index access
            Syntax       ///< General parsing syntax rule violation
        };

        /// @brief An empty traceback, of kind Syntax.
        Traceback() = default;

        /// @brief Records an error.
        /// @param type     What kind of mistake it is.
        /// @param location Where it happened; a zero location means "no position".
        /// @param message  Human-readable explanation; copied.
        Traceback(const Type type, const memory::Location location, const std::string_view message)
            : type_(type), location_(location), message_(message) {}

        [[nodiscard]] constexpr Type type() const noexcept { return type_; }
        [[nodiscard]] constexpr const memory::Location& location() const noexcept { return location_; }
        [[nodiscard]] const std::string& message() const noexcept { return message_; }
        /// @brief Renders as `line:column: error (kind): message`.
        /// @return The rendered string; the position is omitted when it is zero.
        [[nodiscard]] std::string format() const;

        /// @brief Names an error kind.
        /// @param type Kind to name.
        /// @return A short lowercase word, never empty.
        /// @complexity O(1).
        [[nodiscard]] static std::string_view name(Type type) noexcept;

    private:
        Type type_ = Type::Syntax;
        memory::Location location_{};
        std::string message_{};
    };

}