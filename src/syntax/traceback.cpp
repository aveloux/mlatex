/// @file
/// @brief Traceback rendering.
#include "syntax/traceback.hpp"

#include <array>
#include <format>

namespace syntax {

    std::string Traceback::format() const {
        static constexpr std::array<std::string_view, 15> kinds{
            "group", "equation", "environment", "delimiter", "argument", "token", "end-of-input",
            "macro", "recursion", "memory", "scope", "primitive", "dimension", "register", "syntax",
        };
        if (type == Type::Warning) {
            if (location.line == 0) return std::format("warning: {}", message);
            return std::format("{}:{}: warning: {}", location.line, location.column, message);
        }
        const std::string_view kind = kinds[static_cast<std::size_t>(type)];
        if (location.line == 0) return std::format("error ({}): {}", kind, message);
        return std::format("{}:{}: error ({}): {}", location.line, location.column, kind, message);
    }

}
