#include "syntax/traceback.hpp"

#include <cassert>
#include <string>

// A traceback says where, what kind, and what: enough to find the line and
// know what was wrong with it without reading the engine.

int main() {
    constexpr memory::Location location{.line = 14, .column = 2};
    const syntax::Traceback traceback(syntax::Traceback::Type::Macro, location, "Undefined control sequence \\unknown");
    const std::string text = traceback.format();

    assert((text.find("14:2") != std::string::npos) && "the line and column are given");
    assert((text.find("\\unknown") != std::string::npos) && "the message is given whole");
    assert((text.find("macro") != std::string::npos) && "the kind of error is named");
    assert((text.find("error") != std::string::npos) && "it says it is an error");

    const syntax::Traceback other(syntax::Traceback::Type::Register, {.line = 1, .column = 1}, "x");
    assert((other.format().find("register") != std::string::npos) && "each kind is named for itself");

    return 0;
}
