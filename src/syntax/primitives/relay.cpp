/// @file
/// @brief Conditional primitives: the `\\if` family, `\\else`, `\\or`, `\\fi`.
///
/// Every scan here is bounded by Relay::limit, so a missing `\\fi` is
/// reported rather than read to the end of the document.
#include "syntax/primitives/relay.hpp"
#include "logger.hpp"

#include <string>
#include <string_view>

namespace syntax::primitives {

    void Relay::fault(const Traceback::Type type, const memory::Location location,
                      const std::string_view message) const {
        Logger::log(Logger::Type::Semantics, Logger::Level::Error, message);
        faults.emplace_back(type, location, message);
    }

    Relay::Relay(Lexicon& names) noexcept {
        finish = names.intern("\\fi");
        alternative = names.intern("\\else");
        branch = names.intern("\\or");
        // The same register names every other module uses. Scanning against
        // anything else would leave \ifnum unable to read a register at all.
        integer = names.intern("\\integer");
        length = names.intern("\\length");
        terminate = names.intern("\\endcsname");
    }

    void Relay::operator()(Mouth& mouth, Context& context) const {
        mouth.bind("\\iftrue", [this, &mouth](Mouth&) { path(mouth, true); });
        mouth.bind("\\iffalse", [this, &mouth](Mouth&) { path(mouth, false); });
        mouth.bind("\\unless", [this](Mouth&) { inverted = true; });

        mouth.bind("\\ifx", [this, &mouth](Mouth&) {
            const Token first = mouth.read();
            const Token second = mouth.read();
            const Mouth::Macro* primary = mouth.lookup(first.symbol);
            const Mouth::Macro* secondary = mouth.lookup(second.symbol);
            bool condition = false;

            if (primary && secondary) condition = compare(*primary, *secondary);
            else if (primary || secondary) condition = false;
            else {
                const bool start = mouth.primitive(first.symbol) != nullptr;
                const bool end = mouth.primitive(second.symbol) != nullptr;
                condition = start || end ? start && end && first.symbol == second.symbol : true;
            }
            path(mouth, condition);
        });

        mouth.bind("\\ifcat", [this, &mouth](Mouth&) {
            // Sequenced deliberately: the two operands of == are not ordered,
            // so reading both inside the comparison left it to the compiler
            // which token came first.
            const Token first = mouth.read();
            const Token second = mouth.read();
            path(mouth, first.category == second.category);
        });

        mouth.bind("\\if", [this, &mouth](Mouth&) {
            const Token first = mouth.expand();
            const Token second = mouth.expand();
            path(mouth, first.symbol == second.symbol || first.values == second.values);
        });

        mouth.bind("\\ifempty", [this, &mouth](Mouth&) {
            path(mouth, mouth.argument({}, 0).empty());
        });

        mouth.bind("\\ifdefined", [this, &mouth](Mouth&) {
            const Token token = mouth.read();
            path(mouth, mouth.lookup(token.symbol) != nullptr || mouth.primitive(token.symbol) != nullptr);
        });

        mouth.bind("\\ifcsname", [this, &mouth](Mouth&) {
            std::string content;
            std::size_t count = 0uz;

            while (true) {
                if (++count > limit) {
                    fault(Traceback::Type::Recursion, mouth.lookahead().location,
                          "\\ifcsname exceeded scan limit");
                    break;
                }
                const Token token = mouth.expand();
                if (token.symbol == terminate || token.empty()) break;
                content += token.values;
            }
            path(mouth, mouth.lookup(mouth.lexicon().intern(content)) != nullptr);
        });

        mouth.bind("\\ifnum", [this, &context, &mouth](Mouth&) {
            const auto first = mouth.integer(context.ledger, integer);
            const Token relation = mouth.read();
            const auto second = mouth.integer(context.ledger, integer);
            bool condition = false;

            if (first && second) {
                if (relation.is('<')) condition = *first < *second;
                else if (relation.is('>')) condition = *first > *second;
                else if (relation.is('=')) condition = *first == *second;
                else fault(Traceback::Type::Argument, relation.location,
                           "\\ifnum needs one of < > = between its operands");
            }
            path(mouth, condition);
        });

        mouth.bind("\\ifdim", [this, &context, &mouth](Mouth&) {
            const auto first = mouth.dimension(context.ledger, integer, length);
            const Token relation = mouth.read();
            const auto second = mouth.dimension(context.ledger, integer, length);
            bool condition = false;

            if (first && second) {
                if (relation.is('<')) condition = *first < *second;
                else if (relation.is('>')) condition = *first > *second;
                else if (relation.is('=')) condition = *first == *second;
                else fault(Traceback::Type::Argument, relation.location,
                           "\\ifdim needs one of < > = between its operands");
            }
            path(mouth, condition);
        });

        mouth.bind("\\ifodd", [this, &context, &mouth](Mouth&) {
            const auto value = mouth.integer(context.ledger, integer);
            path(mouth, value.has_value() && (*value % 2) != 0);
        });

        mouth.bind("\\ifcase", [this, &context, &mouth](Mouth&) {
            if (inverted) inverted = false;
            std::int32_t remaining = mouth.integer(context.ledger, integer).value_or(0);
            if (remaining < 0) {
                skip(mouth);
                return;
            }

            std::size_t depth = 0uz;
            std::size_t count = 0uz;

            while (remaining > 0) {
                if (++count > limit) {
                    fault(Traceback::Type::Recursion, mouth.lookahead().location,
                          "\\ifcase exceeded scan limit");
                    return;
                }

                const Token token = mouth.read();
                if (token.empty()) return;

                if (token.category == CatCodes::Category::Escape && token.values.starts_with("\\if")) {
                    depth++;
                    continue;
                }
                if (token.symbol == finish) {
                    if (depth == 0uz) return;
                    depth--;
                    continue;
                }
                if (depth == 0uz && token.symbol == alternative) return;
                if (depth == 0uz && token.symbol == branch) remaining--;
            }
        });

        mouth.bind("\\else", [this, &mouth](Mouth&) { drop(mouth); });
        mouth.bind("\\or", [this, &mouth](Mouth&) { drop(mouth); });
        mouth.bind("\\fi", [](Mouth&) {});

        Logger::log(Logger::Type::Semantics, Logger::Level::Debug, "Bound conditional primitives");
    }

    bool Relay::compare(const Mouth::Macro& first, const Mouth::Macro& second) noexcept {
        if (first.parameters.size() != second.parameters.size()) return false;
        if (first.body.size() != second.body.size()) return false;
        for (std::size_t index = 0uz; index < first.body.size(); ++index) {
            if (first.body[index].symbol != second.body[index].symbol) return false;
            if (first.body[index].values != second.body[index].values) return false;
        }
        return true;
    }

    void Relay::skip(Mouth& mouth) const {
        std::size_t depth = 0uz;
        std::size_t count = 0uz;

        while (true) {
            if (++count > limit) {
                fault(Traceback::Type::Recursion, mouth.lookahead().location,
                      "conditional skip exceeded scan limit");
                return;
            }

            const Token token = mouth.read();
            if (token.empty()) {
                fault(Traceback::Type::End, mouth.lookahead().location,
                      "conditional reached the end of the document without \\fi");
                return;
            }

            if (token.category == CatCodes::Category::Escape && token.values.starts_with("\\if")) {
                depth++;
                continue;
            }
            if (token.symbol == finish) {
                if (depth == 0uz) return;
                depth--;
                continue;
            }
            if (depth == 0uz && token.symbol == alternative) return;
        }
    }

    void Relay::drop(Mouth& mouth) const {
        std::size_t depth = 0uz;
        std::size_t count = 0uz;

        while (true) {
            if (++count > limit) {
                fault(Traceback::Type::Recursion, mouth.lookahead().location,
                      "conditional drop exceeded scan limit");
                return;
            }

            const Token token = mouth.read();
            if (token.empty()) {
                fault(Traceback::Type::End, mouth.lookahead().location,
                      "conditional reached the end of the document without \\fi");
                return;
            }

            if (token.category == CatCodes::Category::Escape && token.values.starts_with("\\if")) {
                depth++;
                continue;
            }
            if (token.symbol == finish) {
                if (depth == 0uz) return;
                depth--;
            }
        }
    }

    void Relay::path(Mouth& mouth, bool condition) const {
        if (inverted) {
            condition = !condition;
            inverted = false;
        }
        if (!condition) skip(mouth);
    }

}