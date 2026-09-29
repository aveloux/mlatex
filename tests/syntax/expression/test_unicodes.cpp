#include "syntax/expression/unicodes.hpp"

#include <cassert>

// The maths symbol table: a name, without its backslash, to the character
// it draws and the kind of atom it makes -- which decides the space either
// side of it. Built from assets/unicodes.gperf, a perfect hash: one probe.

int main() {
    using Category = syntax::expression::Unicodes::Category;
    const syntax::expression::Unicodes unicodes;

    const auto alpha = unicodes.query("alpha");
    assert((alpha && alpha->codepoint == 0x03B1 && alpha->category == Category::Ordinary) && "\\alpha is a letter");
    const auto le = unicodes.query("le");
    assert((le && le->codepoint == 0x2264 && le->category == Category::Relation) && "\\le is a relation");
    const auto times = unicodes.query("times");
    assert((times && times->codepoint == 0x00D7 && times->category == Category::Binary) &&
           "\\times is a binary operator");
    const auto sum = unicodes.query("sum");
    assert((sum && sum->codepoint == 0x2211 && sum->category == Category::Operator) && "\\sum is a large operator");
    const auto open = unicodes.query("langle");
    assert((open && open->category == Category::Opening) && "\\langle opens");
    const auto close = unicodes.query("rangle");
    assert((close && close->category == Category::Closing) && "\\rangle closes");
    const auto bracket = unicodes.query("lBrack");
    assert((bracket && bracket->codepoint == 0x27E6) && "the double bracket stmaryrd's \\llbracket stands for");
    const auto upright = unicodes.query("mupalpha");
    assert((upright && upright->codepoint == 0x03B1) && "an upright alpha, which upgreek's \\upalpha stands for");
    assert((!unicodes.query("nosuchsymbol").has_value()) && "a name that is no symbol is nothing");
    assert((!unicodes.query("").has_value()) && "and so is no name at all");
    return 0;
}
