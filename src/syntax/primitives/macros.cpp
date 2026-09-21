/// @file
/// @brief Definition primitives: `\\define`, `\\forget`, `\\alias` and prefixes.
#include "syntax/primitives/macros.hpp"
#include "syntax/primitives/scanning.hpp"
#include "logger.hpp"

#include <string_view>

#include <format>

namespace syntax::primitives {

    void Macros::fault(const Traceback::Type type, const memory::Location location,
                   const std::string_view message) const {
        Logger::log(Logger::Type::Semantics, Logger::Level::Error, message);
        faults.emplace_back(type, location, message);
    }

    Macros::Macros(Lexicon& names) noexcept {
        integer = names.intern("\\integer");
    }

    void Macros::operator()(Mouth& mouth, Context& context) const {
        // Prefixes. Each sets a flag the expander consumes on the next
        // definition, so they compose and may be written in any order.
        mouth.bind("\\shared",   [&mouth](Mouth&) { mouth.globalize(); });
        mouth.bind("\\spanning", [&mouth](Mouth&) { mouth.longify(); });
        mouth.bind("\\guarded",  [&mouth](Mouth&) { mouth.outerize(); });

        mouth.bind("\\define", [this, &context, &mouth](Mouth&) {
            const Token name = mouth.read();
            if (name.category != CatCodes::Category::Escape) {
                fault(Traceback::Type::Macro, name.location,
                              "\\define needs a control sequence to name");
                return;
            }

            // Optional [n] parameter count. Read before the body, because the
            // body's #1..#n only mean anything once n is known.
            int declared = 0;
            if (mouth.lookahead().is('[')) {
                mouth.read();
                prime(mouth);
                const auto scanned = mouth.integer(context.ledger, integer);
                if (!scanned || *scanned < 0 || *scanned > parameters) {
                    fault(Traceback::Type::Argument, name.location,
                                  std::format("\\define{} takes between 0 and {} parameters",
                                              name.values, parameters));
                    return;
                }
                declared = *scanned;
                if (!mouth.lookahead().is(']')) {
                    fault(Traceback::Type::Argument, name.location,
                                  "\\define parameter count is missing its ']'");
                    return;
                }
                mouth.read();
            }

            // Flags are consumed here, before argument() runs, so that a
            // prefix cannot leak onto whatever the body happens to contain.
            const int spanning = mouth.unlong();
            const int isolated = mouth.unouter();

            Mouth::Macro macro;
            macro.parameters.resize(static_cast<std::size_t>(declared));
            macro.body = mouth.argument({}, spanning);
            macro.spanning = spanning != 0;
            macro.isolated = isolated != 0;

            mouth.define(name.symbol, std::move(macro));
        });

        mouth.bind("\\forget", [this, &mouth](Mouth&) {
            const Token name = mouth.read();
            if (name.category != CatCodes::Category::Escape) {
                fault(Traceback::Type::Macro, name.location,
                              "\\forget needs a control sequence");
                return;
            }
            if (mouth.lookup(name.symbol) == nullptr) {
                fault(Traceback::Type::Macro, name.location,
                              std::format("\\forget{} is not defined", name.values));
                return;
            }
            mouth.undefine(name.symbol);
        });

        mouth.bind("\\alias", [this, &mouth](Mouth&) {
            const Token name = mouth.read();
            const Token source = mouth.read();

            if (name.category != CatCodes::Category::Escape ||
                source.category != CatCodes::Category::Escape) {
                fault(Traceback::Type::Macro, name.location,
                              "\\alias needs two control sequences");
                return;
            }

            if (const Mouth::Macro* found = mouth.lookup(source.symbol)) {
                mouth.define(name.symbol, *found);
                return;
            }

            // Aliasing a primitive copies its handler, so the new name behaves
            // identically rather than merely expanding to the old one.
            if (const Mouth::Handler* handler = mouth.primitive(source.symbol)) {
                mouth.bind(name.symbol, *handler);
                return;
            }

            fault(Traceback::Type::Macro, source.location,
                          std::format("\\alias source {} is not defined", source.values));
        });

        Logger::log(Logger::Type::Semantics, Logger::Level::Debug, "Bound definition primitives");
    }

}