/// @file
/// @brief Hooks implementation: named token lists, added to and run.
#include "syntax/primitives/hooks.hpp"
#include "syntax/argument.hpp"
#include "logger.hpp"

#include <span>

namespace syntax::primitives {

    Hooks::Hooks(Lexicon& lexicon) noexcept {
        lexicon.intern("\\addtohook");
        lexicon.intern("\\usehook");
    }

    void Hooks::run(Mouth& mouth, const std::string_view name) const {
        // Injected as a copy: the hook keeps its code for the next time it
        // runs, and running it may add to it.
        const auto found = hooks.find(name);
        if (found == hooks.end() || found->second.empty()) return;

        const std::vector<Token> code = found->second;
        mouth.stream().inject(std::span{code});
    }

    void Hooks::operator()(Mouth& mouth, Context&) const {
        mouth.bind("\\addtohook", [this](Mouth& mouth) {
            const std::string name = Argument::text(mouth);
            const std::vector<Token> code = mouth.argument({}, 1);

            std::vector<Token>& kept = hooks[name];
            kept.insert(kept.end(), code.begin(), code.end());
            Logger::log(Logger::Type::Mouth, Logger::Level::Debug,
                        "Hook '{}' now runs {} token(s)", name, kept.size());
        });

        mouth.bind("\\usehook", [this](Mouth& mouth) { run(mouth, Argument::text(mouth)); });

        Logger::log(Logger::Type::Mouth, Logger::Level::Debug, "Bound the hook primitives");
    }

}
