/// @file
/// @brief Installs the core primitive set and gathers what it reported.
///
/// The bind() calls are independent -- a later module may rebind a name an
/// earlier one took, and the last one installed wins.
#include "syntax/primitives/wrapper.hpp"
#include "logger.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace syntax::primitives {

    Wrapper::Wrapper(Lexicon& lexicon) noexcept
        : relay(lexicon), blocks(lexicon), macros(lexicon), values(lexicon),
          compute(lexicon), loops(lexicon), include(lexicon),
          variables_(lexicon), decimals(lexicon), hooks(lexicon) {}

    void Wrapper::operator()(Mouth& mouth, Context& context) const {
        // One fold over every module. Adding a primitive means adding a member
        // and a name here; nothing else changes.
        bind(mouth, context, relay, blocks, macros, values, compute, loops, include,
             variables_, decimals, hooks);

        // Every word of the engine's own that is not LaTeX's, a second time
        // under an `@` name -- the way LaTeX's own internals are named, out of
        // a document's reach. A document may take `\\set` or `\\define` for
        // itself, and its definition wins from there on; the packages write
        // `\\@set` and `\\@define`, which it never touches, so what they do
        // does not change under it.
        // The register banks' own names are not among them: a register is
        // told by its name's symbol when a number is read, so each has one.
        static constexpr std::array<std::string_view, 39> vocabulary{
            "define", "forget", "alias", "shared", "spanning", "guarded", "set", "increase", "scale", "reduce",
            "name", "evaluate", "repeat", "group", "ungroup", "enter", "leave",
            "variable", "setvariable", "unsetvariable", "ifvariable", "setkeys", "calculate", "amount",
            "separators", "addtohook", "usehook", "requirepackage", "providepackage", "ifempty", "ifstar",
            "ifnextchar", "ifpackageloaded", "ifstrequal", "provided", "expanded", "declare", "switch",
            "iffile",
        };
        for (const std::string_view word : vocabulary) {
            const Symbol plain = mouth.lexicon().intern("\\" + std::string(word));
            mouth.lend(mouth.lexicon().intern("\\@" + std::string(word)), plain);
        }

        // LaTeX's two document hooks: what \\AtBeginDocument gathered runs as
        // the document's own block opens, and what \\AtEndDocument gathered
        // as it closes -- the one place two modules here meet, which is why
        // it is wired here rather than in either.
        blocks.watch(
            "document",
            [this](Mouth& mouth) { hooks.run(mouth, "begindocument"); },
            [this](Mouth& mouth) { hooks.run(mouth, "enddocument"); });

        Logger::log(Logger::Type::Semantics, Logger::Level::Informative,
                    "Core primitives installed");
    }

    std::vector<Traceback> Wrapper::tracebacks() const {
        // Every module, conditionals included: relay keeps its own list like
        // the rest, so leaving it out here lost runaway and unclosed-\if
        // reports even though the module had recorded them.
        const std::vector<Traceback>* lists[] = {
            &relay.tracebacks(), &blocks.tracebacks(), &macros.tracebacks(),
            &values.tracebacks(), &compute.tracebacks(), &loops.tracebacks(),
            &include.tracebacks(), &variables_.tracebacks(), &decimals.tracebacks(),
            &hooks.tracebacks(),
        };

        std::size_t total = 0uz;
        for (const auto* list : lists) total += list->size();

        std::vector<Traceback> gathered;
        gathered.reserve(total);
        for (const auto* list : lists) {
            gathered.insert(gathered.end(), list->begin(), list->end());
        }
        return gathered;
    }

}