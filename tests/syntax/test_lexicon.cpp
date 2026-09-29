#include "memory/arena.hpp"
#include "syntax/lexicon.hpp"

#include <cassert>
#include <string>

// The lexicon: every name interned once, as a small number every table is
// indexed by, and resolved back to its text.

int main() {
    memory::Arena arena(1u << 16);
    syntax::Lexicon lexicon(arena);

    const syntax::Symbol relax = lexicon.intern("\\relax");
    const std::size_t size = lexicon.size();
    assert((lexicon.intern("\\relax") == relax) && "a name interned twice is one symbol");
    assert((lexicon.size() == size) && "and takes no second entry");
    assert((lexicon.resolve(relax) == "\\relax") && "a symbol resolves to its name");

    const syntax::Symbol other = lexicon.intern("\\par");
    assert((other != relax) && "two names are two symbols");
    assert((lexicon.resolve(other) == "\\par") && "each resolves to its own");

    // A name from a buffer that goes away is kept by the lexicon itself.
    syntax::Symbol kept = syntax::none;
    {
        std::string passing = "\\transient";
        kept = lexicon.intern(passing);
        passing.assign("overwritten");
    }
    assert((lexicon.resolve(kept) == "\\transient") && "a name outlives the text it was interned from");

    // Many names, each its own.
    for (int index = 0; index < 1000; ++index) lexicon.intern("\\name" + std::to_string(index));
    assert((lexicon.resolve(lexicon.intern("\\name500")) == "\\name500") && "a thousand names stay apart");

    return 0;
}
