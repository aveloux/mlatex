#pragma once

#include "memory/location.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace syntax {

    /// @brief One recorded error and the position that caused it.
    ///
    /// Errors are collected rather than thrown: as in TeX, a mistake costs the
    /// command that made it and processing carries on.
    class Traceback {
    public:
        /// @brief The kind of error.
        enum class Type : std::uint8_t {
            Group,         ///< Unbalanced or unclosed brace group.
            Equation,      ///< Malformed formula.
            Environment,   ///< Mismatched `\begin` and `\end`.
            Delimiter,     ///< Unbalanced delimiter or runaway delimited argument.
            Argument,      ///< Missing or malformed argument.
            Token,         ///< Illegal input byte.
            End,           ///< Input ended too early.
            Macro,         ///< Undefined or misused control sequence.
            Recursion,     ///< Runaway expansion.
            Memory,        ///< A size limit was exceeded.
            Scope,         ///< Unbalanced group exit.
            Primitive,     ///< A primitive could not do what it was asked.
            Dimension,     ///< Missing or malformed dimension.
            Register,      ///< Bad register reference or arithmetic.
            Syntax,        ///< Any other malformed input.
            Warning        ///< Not a mistake: something set in a way of the engine's own, or left out,
                           ///< that a document should hear about -- a package it only knows by name.
        };

        /// @brief Records an error.
        /// @param type     Kind of error.
        /// @param location Where it happened; zero means unknown.
        /// @param message  Explanation; copied.
        Traceback(const Type type, const memory::Location location, const std::string_view message)
            : type_(type), location_(location), message_(message) {}

        /// @brief Kind of error.
        [[nodiscard]] Type type() const noexcept { return type_; }

        /// @brief Where it happened.
        [[nodiscard]] const memory::Location& location() const noexcept { return location_; }

        /// @brief Explanation.
        [[nodiscard]] const std::string& message() const noexcept { return message_; }

        /// @brief Whether it stops the document being clean: every kind but a warning does.
        [[nodiscard]] bool fatal() const noexcept { return type_ != Type::Warning; }

        /// @brief Renders as `line:column: error (kind): message`, or
        ///        `line:column: warning: message`.
        /// @return The rendered text; the position is omitted when unknown.
        [[nodiscard]] std::string format() const;

    private:
        Type type_;                  ///< Kind of error.
        memory::Location location_;  ///< Where it happened.
        std::string message_;        ///< Explanation.
    };

}
