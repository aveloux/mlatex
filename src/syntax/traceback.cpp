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
        if (type_ == Type::Warning) {
            if (location_.line == 0) return std::format("warning: {}", message_);
            return std::format("{}:{}: warning: {}", location_.line, location_.column, message_);
        }
        const std::string_view kind = kinds[static_cast<std::size_t>(type_)];
        if (location_.line == 0) return std::format("error ({}): {}", kind, message_);
        return std::format("{}:{}: error ({}): {}", location_.line, location_.column, kind, message_);
    }

}
