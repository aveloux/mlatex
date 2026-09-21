/// @file
/// @brief Traceback rendering: error kind names and the one-line format.
#include "syntax/traceback.hpp"

#include <format>

namespace syntax {

    std::string_view Traceback::name(const Type type) noexcept {
        using enum Type;
        switch (type) {
            case Group:       return "group";
            case Equation:    return "equation";
            case Environment: return "environment";
            case Delimiter:   return "delimiter";
            case Argument:    return "argument";
            case Token:       return "token";
            case End:         return "end-of-input";
            case Macro:       return "macro";
            case Recursion:   return "recursion";
            case Memory:      return "memory";
            case Catcode:     return "catcode";
            case Scope:       return "scope";
            case Primitive:   return "primitive";
            case Dimension:   return "dimension";
            case Register:    return "register";
            case Syntax:      return "syntax";
        }
        return "syntax";
    }

    std::string Traceback::format() const {
        if (location_.line == 0 && location_.column == 0) {
            return std::format("error ({}): {}", name(type_), message_);
        }
        return std::format("{}:{}: error ({}): {}", location_.line, location_.column, name(type_), message_);
    }

}