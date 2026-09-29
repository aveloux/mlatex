#include "memory/arena.hpp"
#include "syntax/lexicon.hpp"
#include "syntax/mouth.hpp"
#include "syntax/primitives/context.hpp"
#include "syntax/primitives/wrapper.hpp"
#include "syntax/semantics/union.hpp"

#include <cassert>
#include <string>

// The syntax Wrapper: every syntax primitive folded into one callable that
// binds them all into an expander, and one list of what any of them
// reported, merged on demand.

int main() {
    memory::Arena arena(1u << 20);
    syntax::semantics::Union state{};
    syntax::Lexicon lexicon(arena);
    syntax::Mouth mouth(syntax::Cursor{}, state, lexicon, arena);

    syntax::primitives::Wrapper core(lexicon);
    syntax::primitives::Context context{state.registers(), core.conditionals(), core.variables()};
    core(mouth, context);

    // Each module's names are bound: one from each of several.
    for (const char* name : {"\\define", "\\set", "\\ifnum", "\\begin", "\\repeat", "\\evaluate", "\\calculate",
                             "\\setvariable", "\\addtohook", "\\usepackage"}) {
        mouth.ingest(name);
        const syntax::Token token = mouth.read();
        assert((mouth.known(token.symbol)) && name);
        while (!mouth.read().empty()) {}
    }

    // Faults from two modules arrive in one list.
    mouth.ingest("\\end{a}\\variable{never}");
    while (!mouth.expand().empty()) {}
    const std::vector<syntax::Traceback>& faults = core.tracebacks();
    bool block = false;
    bool variable = false;
    for (const syntax::Traceback& fault : faults) {
        block = block || fault.format().find("Extra \\end") != std::string::npos;
        variable = variable || fault.format().find("never") != std::string::npos;
    }
    assert((block && variable) && "what every module reported is gathered into one list");

    return 0;
}
