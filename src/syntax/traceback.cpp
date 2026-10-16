/// @file
/// @brief Traceback rendering.
#include "syntax/traceback.hpp"

#include <array>
#include <format>

namespace syntax {
    std::string Traceback::format(const std::span<const std::string> files) const {
        static constexpr std::array<std::string_view, 15> kinds{
            "group", "equation", "environment", "delimiter", "argument", "token", "end-of-input",
            "macro", "recursion", "memory", "scope", "primitive", "dimension", "register", "syntax",
        };
        // Where, as a compiler says it: the file when it has a name, then the
        // line and column, when they are known.
        std::string where;
        if (location.line != 0 && location.file < files.size() && !files[location.file].empty()) {
            where = std::format("{}:{}:{}: ", files[location.file], location.line, location.column);
        } else if (location.line != 0) {
            where = std::format("{}:{}: ", location.line, location.column);
        }
        if (type == Type::Warning) return std::format("{}warning: {}", where, message);
        return std::format("{}error ({}): {}", where, kinds[static_cast<std::size_t>(type)], message);
    }
}
