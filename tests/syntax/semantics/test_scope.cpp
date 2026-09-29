#include "syntax/semantics/scope.hpp"

#include <cassert>

// Every kind of scope has a name an error message can use.

int main() {
    using Scope = syntax::semantics::Scope;
    using Type = Scope::Type;

    assert((Scope::name(Type::Group) == "group") && "a group");
    assert((Scope::name(Type::Environment) == "environment") && "an environment");
    assert((Scope::name(Type::Equations) == "equation") && "an equation");
    assert((Scope::name(Type::Box) == "box") && "a box");
    assert((Scope::name(Type::Conditional) == "conditional") && "a conditional");
    assert((Scope::name(Type::Alignment) == "alignment") && "an alignment");

    return 0;
}
