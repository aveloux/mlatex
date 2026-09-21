/// @file
/// @brief Block structure primitives: `\\enter` and `\\leave`.
#include "syntax/primitives/blocks.hpp"
#include "logger.hpp"

#include <string_view>

#include <format>
#include <string>

namespace syntax::primitives {

    void Blocks::fault(const Traceback::Type type, const memory::Location location,
                   const std::string_view message) const {
        Logger::log(Logger::Type::Semantics, Logger::Level::Error, message);
        faults.emplace_back(type, location, message);
    }

    Blocks::Blocks(Lexicon& table) noexcept : names(&table) {}

    Symbol Blocks::title(Mouth& mouth) const {
        // A default Parameter reads one brace group and strips the braces, so
        // this yields the characters of the name and nothing else.
        const std::vector<Token> gathered = mouth.argument({}, 0);

        std::string joined;
        joined.reserve(gathered.size());
        for (const Token& token : gathered) {
            joined += token.values;
        }

        if (joined.empty()) return kInvalidSymbol;
        return mouth.lexicon().intern(joined);
    }

    std::string_view Blocks::innermost() const noexcept {
        if (open.empty() || names == nullptr) return {};
        return names->resolve(open.back());
    }

    void Blocks::operator()(Mouth& mouth, Context&) const {
        mouth.bind("\\enter", [this, &mouth](Mouth&) {
            const Symbol name = title(mouth);
            if (name == kInvalidSymbol) {
                fault(Traceback::Type::Environment, mouth.lookahead().location,
                               "\\enter needs a block name");
                return;
            }
            open.push_back(name);
            mouth.push(semantics::Scope::Type::Environment);
        });

        mouth.bind("\\leave", [this, &mouth](Mouth&) {
            const Symbol name = title(mouth);
            if (name == kInvalidSymbol) {
                fault(Traceback::Type::Environment, mouth.lookahead().location,
                               "\\leave needs a block name");
                return;
            }

            if (open.empty()) {
                fault(Traceback::Type::Environment, mouth.lookahead().location,
                               std::format("\\leave{{{}}} with no block open",
                                           mouth.lexicon().resolve(name)));
                return;
            }

            if (open.back() != name) {
                fault(Traceback::Type::Environment, mouth.lookahead().location,
                               std::format("\\leave{{{}}} closes a block opened as {{{}}}",
                                           mouth.lexicon().resolve(name),
                                           mouth.lexicon().resolve(open.back())));
                // Still close it: leaving the scope open would cascade every
                // later \leave into the same complaint.
            }

            open.pop_back();
            mouth.pop(semantics::Scope::Type::Environment);
        });

        Logger::log(Logger::Type::Semantics, Logger::Level::Debug, "Bound block primitives");
    }

}