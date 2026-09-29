#include "syntax/semantics/registers.hpp"

#include <cassert>

// Registers are scoped the way TeX's are: an assignment inside a group is
// undone when the group ends, unless it was global, and each bank keeps its
// own slots.

using Registers = syntax::semantics::Registers;
using Type = Registers::Type;

int main() {
    Registers registers;

    assert((registers.get(Type::Count, 0) == 0) && "a register starts at zero");

    registers.set(Type::Count, 0, 42, false);
    assert((registers.get(Type::Count, 0) == 42) && "a value set is read back");

    registers.push();
    registers.set(Type::Count, 0, 100, false);
    assert((registers.get(Type::Count, 0) == 100) && "a group may change it");
    registers.pop();
    assert((registers.get(Type::Count, 0) == 42) && "and the change ends with the group");

    registers.push();
    registers.set(Type::Count, 1, 7, true);
    registers.pop();
    assert((registers.get(Type::Count, 1) == 7) && "a global assignment outlives the group");

    // Several levels deep, each restored in turn.
    registers.push();
    registers.set(Type::Count, 2, 1, false);
    registers.push();
    registers.set(Type::Count, 2, 2, false);
    registers.set(Type::Count, 2, 3, false);
    assert((registers.get(Type::Count, 2) == 3) && "the latest assignment wins");
    registers.pop();
    assert((registers.get(Type::Count, 2) == 1) && "the inner group's changes are undone");
    registers.pop();
    assert((registers.get(Type::Count, 2) == 0) && "and then the outer group's");

    // The banks are separate.
    registers.set(Type::Dimension, 0, 65536, false);
    registers.set(Type::Glue, 0, 131072, false);
    assert((registers.get(Type::Count, 0) == 42) && "a dimension does not overwrite a count");
    assert((registers.get(Type::Dimension, 0) == 65536) && "a dimension is kept in scaled points");
    assert((registers.get(Type::Glue, 0) == 131072) && "glue is kept by its natural width");

    // A name bound to a slot reads as that slot.
    registers.bind(500, Type::Count, 3);
    const auto target = registers.target(500);
    assert((target && target->type == Type::Count && target->slot == 3) && "a named register addresses its slot");
    assert((!registers.target(501)) && "an unbound name addresses nothing");

    registers.quad = 12 * 65536;
    assert((registers.quad == 12 * 65536) && "an em is what the body face says it is");

    return 0;
}
