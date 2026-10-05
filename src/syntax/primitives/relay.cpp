/// @file
/// @brief Conditional primitives: the `\\if` family, `\\else`, `\\or`, `\\fi`.
///
/// Every scan here is bounded by Relay::limit, so a missing `\\fi` is
/// reported rather than read to the end of the document.
#include "syntax/primitives/relay.hpp"
#include "syntax/number.hpp"
#include "logger.hpp"

#include <algorithm>
#include <string>
#include <string_view>

namespace syntax::primitives {

    Relay::Relay(Lexicon& lexicon) noexcept {
        finish = lexicon.intern("\\fi");
        alternative = lexicon.intern("\\else");
        branch = lexicon.intern("\\or");
        // The same register names every other module uses. Scanning against
        // anything else would leave \ifnum unable to read a register at all.
        integer = lexicon.intern("\\integer");
        length = lexicon.intern("\\length");
        terminate = lexicon.intern("\\endcsname");
        forms = {lexicon.intern("\\ifstar"), lexicon.intern("\\ifnextchar"), lexicon.intern("\\ifstrequal"),
                 lexicon.intern("\\ifvariable"), lexicon.intern("\\ifpackageloaded"), lexicon.intern("\\@ifstar"),
                 lexicon.intern("\\@ifnextchar"), lexicon.intern("\\@ifstrequal"), lexicon.intern("\\@ifvariable"),
                 lexicon.intern("\\@ifpackageloaded"), lexicon.intern("\\iffile"), lexicon.intern("\\@iffile"),
                 lexicon.intern("\\iftest"), lexicon.intern("\\@iftest")};
    }

    bool Relay::opens(const Mouth& mouth, const Token& token) const noexcept {
        return token.category == Catcodes::Category::Escape &&
               (token.text.starts_with("\\if") || token.text.starts_with("\\@if")) &&
               mouth.macro(token.symbol) == nullptr && mouth.handler(token.symbol) != nullptr &&
               !std::ranges::contains(forms, token.symbol);
    }

    void Relay::operator()(Mouth& mouth, Context& context) const {
        mouth.bind("\\iftrue", [this, &mouth](Mouth&) { path(mouth, true); });
        mouth.bind("\\iffalse", [this, &mouth](Mouth&) { path(mouth, false); });
        mouth.bind("\\unless", [this](Mouth&) { inverted = true; });

        mouth.bind("\\ifx", [this, &mouth](Mouth&) {
            // A name \let made from a character is that character here, as
            // it is in TeX: `\let\next=[` and then `\ifx\next[` holds.
            const auto meant = [&mouth](const Token token) {
                const Mouth::Macro* found = mouth.macro(token.symbol);
                return found && found->implicit && !found->body.empty() ? found->body.front() : token;
            };
            const Token first = meant(mouth.read());
            const Token second = meant(mouth.read());
            const Mouth::Macro* primary = mouth.macro(first.symbol);
            const Mouth::Macro* secondary = mouth.macro(second.symbol);
            bool condition = false;

            // Two macros are the same when their parameters and bodies are,
            // token for token.
            if (primary && secondary) {
                condition = primary->parameters.size() == secondary->parameters.size() &&
                            std::ranges::equal(primary->body, secondary->body,
                                               [](const Token& left, const Token& right) {
                                                   return left.symbol == right.symbol && left.text == right.text;
                                               });
            }
            else if (primary || secondary) condition = false;
            else {
                // Two primitives are the same only when they are one; two
                // names neither defines are alike, both undefined, as TeX has
                // it; and two characters are the same character in the same
                // category.
                const bool start = mouth.handler(first.symbol) != nullptr;
                const bool end = mouth.handler(second.symbol) != nullptr;
                const bool named = first.category == Catcodes::Category::Escape &&
                                   second.category == Catcodes::Category::Escape;
                condition = start || end ? start && end && mouth.primitive(first.symbol) == mouth.primitive(second.symbol)
                            : named      ? true
                                         : first.text == second.text && first.category == second.category;
            }
            path(mouth, condition);
        });

        // Whether a formula is being read, so a command can set itself as
        // maths either way: `\\ensuremath` is built on it.
        mouth.bind("\\ifmmode", [this, &mouth](Mouth&) { path(mouth, mouth.formula()); });

        // TeX's other modes: between paragraphs, or inside one, as the parser
        // has said while reading. No file is ever open to read, so each is at
        // its end.
        mouth.bind("\\ifhmode", [this, &mouth](Mouth&) { path(mouth, !mouth.formula() && !mouth.vertical); });
        mouth.bind("\\ifvmode", [this, &mouth](Mouth&) { path(mouth, !mouth.formula() && mouth.vertical); });
        mouth.bind("\\ifinner", [this, &mouth](Mouth&) { path(mouth, false); });
        mouth.bind("\\ifeof", [this, &context, &mouth](Mouth&) {
            static_cast<void>(Number::integer(mouth, context.registers));
            path(mouth, true);
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
            path(mouth, first.symbol == second.symbol || first.text == second.text);
        });

        mouth.bind("\\ifempty", [this, &mouth](Mouth&) {
            path(mouth, mouth.argument({}, 0).empty());
        });

        mouth.bind("\\ifdefined", [this, &mouth](Mouth&) {
            path(mouth, mouth.known(mouth.read().symbol));
        });

        mouth.bind("\\ifcsname", [this, &mouth](Mouth&) {
            std::string content = "\\";
            std::size_t count = 0uz;

            while (true) {
                if (++count > limit) {
                    tracebacks.emplace_back(Traceback::Type::Recursion, mouth.lookahead().location,
                          "\\ifcsname exceeded scan limit");
                    break;
                }
                const Token token = mouth.expand();
                if (token.symbol == terminate || token.empty()) break;
                content += token.text;
            }
            path(mouth, mouth.known(mouth.lexicon.intern(content)));
        });

        mouth.bind("\\ifnum", [this, &context, &mouth](Mouth&) {
            const auto first = Number::integer(mouth, context.registers);
            const Token relation = mouth.read();
            const auto second = Number::integer(mouth, context.registers);
            bool condition = false;

            if (first && second) {
                if (relation.is('<')) condition = *first < *second;
                else if (relation.is('>')) condition = *first > *second;
                else if (relation.is('=')) condition = *first == *second;
                else tracebacks.emplace_back(Traceback::Type::Argument, relation.location,
                           "\\ifnum needs one of < > = between its operands");
            }
            path(mouth, condition);
        });

        mouth.bind("\\ifdim", [this, &context, &mouth](Mouth&) {
            const auto first = Number::dimension(mouth, context.registers);
            const Token relation = mouth.read();
            const auto second = Number::dimension(mouth, context.registers);
            bool condition = false;

            if (first && second) {
                if (relation.is('<')) condition = *first < *second;
                else if (relation.is('>')) condition = *first > *second;
                else if (relation.is('=')) condition = *first == *second;
                else tracebacks.emplace_back(Traceback::Type::Argument, relation.location,
                           "\\ifdim needs one of < > = between its operands");
            }
            path(mouth, condition);
        });

        mouth.bind("\\ifodd", [this, &context, &mouth](Mouth&) {
            const auto value = Number::integer(mouth, context.registers);
            path(mouth, value.has_value() && (*value % 2) != 0);
        });

        mouth.bind("\\ifcase", [this, &context, &mouth](Mouth&) {
            if (inverted) inverted = false;
            std::int32_t remaining = Number::integer(mouth, context.registers).value_or(0);
            if (remaining < 0) {
                skip(mouth);
                return;
            }

            std::size_t depth = 0uz;
            std::size_t count = 0uz;

            while (remaining > 0) {
                if (++count > limit) {
                    tracebacks.emplace_back(Traceback::Type::Recursion, mouth.lookahead().location,
                          "\\ifcase exceeded scan limit");
                    return;
                }

                const Token token = mouth.read();
                if (token.empty()) return;

                if (opens(mouth, token)) {
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

    void Relay::skip(Mouth& mouth) const {
        std::size_t depth = 0uz;
        std::size_t count = 0uz;

        while (true) {
            if (++count > limit) {
                tracebacks.emplace_back(Traceback::Type::Recursion, mouth.lookahead().location,
                      "conditional skip exceeded scan limit");
                return;
            }

            const Token token = mouth.read();
            if (token.empty()) {
                tracebacks.emplace_back(Traceback::Type::End, mouth.lookahead().location,
                      "conditional reached the end of the document without \\fi");
                return;
            }

            if (opens(mouth, token)) {
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
                tracebacks.emplace_back(Traceback::Type::Recursion, mouth.lookahead().location,
                      "conditional drop exceeded scan limit");
                return;
            }

            const Token token = mouth.read();
            if (token.empty()) {
                tracebacks.emplace_back(Traceback::Type::End, mouth.lookahead().location,
                      "conditional reached the end of the document without \\fi");
                return;
            }

            if (opens(mouth, token)) {
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