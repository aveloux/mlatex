/// @file
/// @brief Scope implementation: the runtime stack of open groups.
#include "syntax/semantics/scope.hpp"
#include "logger.hpp"

namespace syntax::semantics {

    void Scope::push(const Type type) {
        stack.push_back(Layer{type, stack.size() + 1uz});
        Logger::fmt(Logger::Type::Semantics, Logger::Level::Debug,
                    "Entered {} scope layer (depth: {})", name(type), stack.size());
    }

    void Scope::pop() {
        if (stack.empty()) {
            Logger::log(Logger::Type::Semantics, Logger::Level::Warning,
                        "Attempted pop operation on empty scope stack");
            return;
        }
        const Type type = stack.back().type;
        stack.pop_back();
        Logger::fmt(Logger::Type::Semantics, Logger::Level::Debug,
                    "Exited {} scope layer (depth remaining: {})", name(type), stack.size());
    }

    std::size_t Scope::depth() const noexcept {
        return stack.size();
    }

    Scope::Type Scope::type() const noexcept {
        if (stack.empty()) {
            return Type::Group;
        }
        return stack.back().type;
    }

    std::optional<Scope::Layer> Scope::top() const noexcept {
        if (stack.empty()) return std::nullopt;
        return stack.back();
    }

    void Scope::reset() noexcept {
        stack.clear();
        Logger::log(Logger::Type::Semantics, Logger::Level::Informative,
                    "Scope hierarchy reset to root level");
    }

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