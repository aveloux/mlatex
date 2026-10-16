#include "syntax/traceback.hpp"

#include <array>
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

    // The file comes first, when the location's has a name.
    const std::array<std::string, 2> files{"", "appendix.tex"};
    const syntax::Traceback filed(syntax::Traceback::Type::Macro, {.line = 3, .column = 5, .file = 1},
                                  "Undefined control sequence \\foo");
    assert((filed.format(files) == "appendix.tex:3:5: error (macro): Undefined control sequence \\foo") &&
           "a position names the file it is in");
    assert((filed.format() == "3:5: error (macro): Undefined control sequence \\foo") &&
           "and only the line and column without the names");
    const syntax::Traceback unnamed(syntax::Traceback::Type::Warning, location, "loose");
    assert((unnamed.format(files) == "14:2: warning: loose") && "text that is no file's names none");
    const syntax::Traceback nowhere(syntax::Traceback::Type::Warning, {.line = 0, .column = 0, .file = 1}, "vague");
    assert((nowhere.format(files) == "warning: vague") && "nor does a location with no line");

    return 0;
}
