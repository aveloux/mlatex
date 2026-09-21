/// @file
/// @brief Iteration primitives: `\\repeat`, `\\group`, `\\ungroup`.
#include "syntax/primitives/loops.hpp"
#include "syntax/primitives/scanning.hpp"
#include "logger.hpp"

#include <string_view>

#include <format>
#include <vector>

namespace syntax::primitives {

    void Loops::fault(const Traceback::Type type, const memory::Location location,
                   const std::string_view message) const {
        Logger::log(Logger::Type::Semantics, Logger::Level::Error, message);
        faults.emplace_back(type, location, message);
    }

    Loops::Loops(Lexicon& names) noexcept {
        integer = names.intern("\\integer");
    }

    void Loops::operator()(Mouth& mouth, Context& context) const {
        mouth.bind("\\repeat", [this, &context, &mouth](Mouth&) {
            if (!mouth.lookahead().is('[')) {
                fault(Traceback::Type::Argument, mouth.lookahead().location,
                              "\\repeat needs a count in brackets");
                return;
            }
            mouth.read();

            prime(mouth);
            const auto count = mouth.integer(context.ledger, integer);
            if (!count || *count < 0 || *count > passes) {
                fault(Traceback::Type::Argument, mouth.lookahead().location,
                              std::format("\\repeat count must be between 0 and {}", passes));
                return;
            }

            if (!mouth.lookahead().is(']')) {
                fault(Traceback::Type::Argument, mouth.lookahead().location,
                              "\\repeat count is missing its ']'");
                return;
            }
            mouth.read();

            const std::vector<Token> body = mouth.argument({}, 0);
            if (body.empty() || *count == 0) return;

            const std::size_t total = body.size() * static_cast<std::size_t>(*count);
            if (total > production) {
                fault(Traceback::Type::Memory, mouth.lookahead().location,
                              std::format("\\repeat would produce {} tokens, past the limit of {}",
                                          total, production));
                return;
            }

            // Built once and injected once: the expander never re-enters, so
            // this cannot run away the way a recursive loop macro would.
            std::vector<Token> produced;
            produced.reserve(total);
            for (std::int32_t pass = 0; pass < *count; ++pass) {
                produced.insert(produced.end(), body.begin(), body.end());
            }
            mouth.inject(produced);
        });

        mouth.bind("\\group", [&mouth](Mouth&) {
            mouth.push(semantics::Scope::Type::Group);
        });

        mouth.bind("\\ungroup", [&mouth](Mouth&) {
            mouth.pop(semantics::Scope::Type::Group);
        });

        Logger::log(Logger::Type::Semantics, Logger::Level::Debug, "Bound iteration primitives");
    }

}