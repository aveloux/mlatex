/// @file
/// @brief Scope implementation: naming a kind, and nothing else.
#include "syntax/semantics/scope.hpp"

namespace syntax::semantics {

    std::string_view Scope::name(const Type type) noexcept {
        using enum Type;
        switch (type) {
            case Group:       return "group";
            case Environment: return "environment";
            case Equations:   return "equation";
            case Box:         return "box";
            case Conditional: return "conditional";
            case Alignment:   return "alignment";
        }
        return "group";
    }

}
