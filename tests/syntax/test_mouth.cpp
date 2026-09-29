#include "syntax/mouth.hpp"
#include "syntax/semantics/union.hpp"

#include <cassert>
#include <string>

// The expander: scopes that nest and are checked as they close, macros that
// are scoped with them, and expansion that reads a macro's body in its place.

/// @brief Every token's text the expander hands out, until it runs dry.
static std::string drain(syntax::Mouth& mouth) {
    std::string text;
    for (syntax::Token token = mouth.expand(); !token.empty(); token = mouth.expand()) text += token.text;
    return text;
}

int main() {
    memory::Arena arena;
    syntax::semantics::Union state;
    syntax::Lexicon lexicon(arena);
    syntax::Mouth mouth(syntax::Cursor{}, state, lexicon, arena);

    using Type = syntax::semantics::Scope::Type;

    // Nesting is Mouth's own count, and it is exact: three opens is a depth
    // of three, and each close brings it back down by one.
    assert((mouth.nesting() == 0) && "nothing open to begin with");
    mouth.push(Type::Group);
    mouth.push(Type::Box);
    mouth.push(Type::Environment);
    assert((mouth.nesting() == 3) && "three scopes open");
    assert((mouth.pop(Type::Environment)) && "the innermost closes by its own kind");
    assert((mouth.pop(Type::Box)) && "and the next");
    assert((mouth.nesting() == 1) && "one left");

    // Closing the wrong kind is reported rather than silently accepted, which
    // is what lets `\begin{a} ... }` be told apart from `\begin{a} ... \end{a}`.
    assert((!mouth.pop(Type::Alignment)) && "closing the wrong kind fails");
    assert((!mouth.traceback().empty()) && "and is reported");
    assert((mouth.nesting() == 0) && "the scope is closed all the same");

    // A definition made inside a group does not outlive it.
    const syntax::Symbol a = lexicon.intern("\\a");
    const syntax::Symbol b = lexicon.intern("\\b");
    mouth.push(Type::Group);
    mouth.define(a, syntax::Mouth::Macro{.literal = true}, false);
    assert((mouth.macro(a) != nullptr) && "a macro defined in a group exists there");
    mouth.pop(Type::Group);
    assert((mouth.macro(a) == nullptr) && "and is gone after it");

    // `\global` reaches past the group that made it.
    mouth.push(Type::Group);
    mouth.define(b, syntax::Mouth::Macro{.literal = true}, true);
    mouth.pop(Type::Group);
    assert((mouth.macro(b) != nullptr) && "a global definition outlives its group");

    // Expansion: a macro reads as its body, a parameter as its argument.
    {
        syntax::Mouth::Macro twice;
        twice.parameters.resize(1);
        mouth.ingest("#1#1");
        for (syntax::Token token = mouth.read(); !token.empty(); token = mouth.read()) twice.body.push_back(token);
        mouth.define(lexicon.intern("\\twice"), std::move(twice), false);

        mouth.ingest("\\twice{ab}c");
        assert((drain(mouth) == "ababc") && "a macro reads as its body, its parameter as its argument");
    }

    // A primitive bound by a module runs when it is read.
    {
        int calls = 0;
        mouth.bind("\\count", [&calls](syntax::Mouth&) { ++calls; });
        mouth.ingest("\\count x\\count");
        assert((drain(mouth) == "x" && calls == 2) && "a bound primitive runs each time it is read");
    }

    // A primitive that binds others while it runs -- \newif making its
    // switches -- may grow the table it is kept in, and still runs to its
    // end with everything it holds.
    {
        std::string seen;
        const std::string held = "kept";
        mouth.bind("\\grow", [&seen, &lexicon, held](syntax::Mouth& inner) {
            for (int index = 0; index < 4096; ++index) {
                inner.bind(lexicon.intern("\\grown" + std::to_string(index)), [](syntax::Mouth&) {});
            }
            seen = held;
        });
        mouth.ingest("\\grow");
        drain(mouth);
        assert((seen == "kept" && mouth.handler(lexicon.intern("\\grown4095"))) &&
               "a primitive that grows the table under itself still runs whole");
    }

    // What is ingested last is read first.
    mouth.ingest("second");
    mouth.ingest("first ");
    assert((drain(mouth) == "first second") && "the stream is a stack");

    return 0;
}
