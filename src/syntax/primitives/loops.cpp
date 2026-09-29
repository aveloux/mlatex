/// @file
/// @brief Iteration primitives: `\\repeat`, `\\group`, `\\ungroup`.
#include "syntax/primitives/loops.hpp"
#include "syntax/number.hpp"
#include "logger.hpp"

#include <charconv>
#include <cmath>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace syntax::primitives {

    Loops::Loops(Lexicon& lexicon) noexcept {
        integer = lexicon.intern("\\integer");
    }

    void Loops::operator()(Mouth& mouth, Context& context) const {
        mouth.bind("\\repeat", [this, &context, &mouth](Mouth&) {
            if (!mouth.lookahead().is('[')) {
                tracebacks.emplace_back(Traceback::Type::Argument, mouth.lookahead().location,
                              "\\repeat needs a count in brackets");
                return;
            }
            mouth.read();

            const auto count = Number::integer(mouth, context.registers);
            if (!count || *count < 0 || *count > passes) {
                tracebacks.emplace_back(Traceback::Type::Argument, mouth.lookahead().location,
                              std::format("\\repeat count must be between 0 and {}", passes));
                return;
            }

            if (!mouth.lookahead().is(']')) {
                tracebacks.emplace_back(Traceback::Type::Argument, mouth.lookahead().location,
                              "\\repeat count is missing its ']'");
                return;
            }
            mouth.read();

            // A body may end a paragraph each pass, as a loop setting one
            // paragraph a time does.
            const std::vector<Token> body = mouth.argument({}, 1);
            if (body.empty() || *count == 0) return;

            const std::size_t total = body.size() * static_cast<std::size_t>(*count);
            if (total > production) {
                tracebacks.emplace_back(Traceback::Type::Memory, mouth.lookahead().location,
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
            mouth.stream().inject(produced);
        });

        // pgffor's \\foreach, as \\@foreach: `\\foreach \\x in {1,2,...,5} {body}`,
        // `\\foreach \\x/\\y [count=\\i] in {a/1, b/2} \\draw ...;` -- the body
        // braced, or up to the semicolon that ends a path. Each pass is a
        // group of its own, its variables defined in it: every pass built
        // and injected at once, so a loop cannot run away. A list may count
        // on from its first two items with `...`, in numbers or letters.
        mouth.bind("\\@foreach", [this](Mouth& mouth) {
            Lexicon& lexicon = mouth.lexicon;
            const auto character = [&lexicon](const std::string_view text, const Catcodes::Category category) {
                const Symbol symbol = lexicon.intern(text);
                return Token{.symbol = symbol, .category = category, .text = lexicon.resolve(symbol)};
            };
            const auto blank = [&mouth] {
                while (mouth.lookahead().category == Catcodes::Category::Space) mouth.read();
            };

            // The variables, `/` between them, then `[count=\\i]`, then `in`.
            std::vector<Token> variables;
            Token counter{};
            blank();
            while (!mouth.lookahead().empty()) {
                const Token token = mouth.lookahead();
                if (token.category == Catcodes::Category::Escape) {
                    variables.push_back(mouth.read());
                } else if (token.is('/') || token.category == Catcodes::Category::Space) {
                    mouth.read();
                } else if (token.is('[')) {
                    mouth.read();
                    std::string key;
                    for (Token inside = mouth.read(); !inside.empty() && !inside.is(']'); inside = mouth.read()) {
                        if (inside.category == Catcodes::Category::Escape && key.ends_with("count=")) counter = inside;
                        else key += inside.text;
                    }
                } else {
                    break;
                }
            }
            blank();
            if (!mouth.lookahead().is('i')) {
                tracebacks.emplace_back(Traceback::Type::Argument, mouth.lookahead().location, "\\foreach needs 'in' before its list");
                return;
            }
            mouth.read();
            if (mouth.lookahead().is('n')) mouth.read();
            blank();

            // The list, its macros opened, cut at its commas outside braces.
            std::vector<Token> list;
            for (const Token& token : mouth.argument({}, 1)) {
                if (token.category != Catcodes::Category::Escape) {
                    list.push_back(token);
                    continue;
                }
                const Mouth::Macro* macro = mouth.macro(token.symbol);
                if (macro && macro->parameters.empty()) list.insert(list.end(), macro->body.begin(), macro->body.end());
                else list.push_back(token);
            }
            std::vector<std::vector<Token>> items(1);
            int depth = 0;
            for (const Token& token : list) {
                if (token.is(Catcodes::Category::Group, '{')) ++depth;
                if (token.is(Catcodes::Category::Group, '}')) --depth;
                if (depth == 0 && token.is(',')) {
                    items.emplace_back();
                    continue;
                }
                if (token.category == Catcodes::Category::Space && items.back().empty()) continue;
                items.back().push_back(token);
            }
            const auto spelled = [](const std::vector<Token>& tokens) {
                std::string text;
                for (const Token& token : tokens) text += token.text;
                while (!text.empty() && text.back() == ' ') text.pop_back();
                return text;
            };

            // `...`: counted on from what stands before it, in the step its
            // two items before it take, to the item after it.
            for (std::size_t index = 0; index < items.size(); ++index) {
                if (spelled(items[index]) != "..." || index == 0 || index + 1 >= items.size()) continue;
                const std::string from = spelled(items[index - 1]);
                const std::string to = spelled(items[index + 1]);
                std::vector<std::vector<Token>> made;
                if (from.size() == 1 && to.size() == 1 && std::isalpha(static_cast<unsigned char>(from[0]))) {
                    for (char letter = static_cast<char>(from[0] + 1); letter < to[0]; ++letter) {
                        made.push_back({character(std::string_view(&letter, 1), Catcodes::Category::Letter)});
                    }
                } else {
                    double first = 0.0;
                    double end = 0.0;
                    double before = 0.0;
                    std::from_chars(from.data(), from.data() + from.size(), first);
                    std::from_chars(to.data(), to.data() + to.size(), end);
                    double step = 1.0;
                    if (index >= 2) {
                        const std::string earlier = spelled(items[index - 2]);
                        if (std::from_chars(earlier.data(), earlier.data() + earlier.size(), before).ec == std::errc{}) {
                            step = first - before;
                        }
                    }
                    if (step == 0.0 || (end - first) / step > 10000.0) continue;
                    for (double value = first + step; step > 0.0 ? value < end - 1e-9 : value > end + 1e-9; value += step) {
                        const double rounded = std::round(value * 1e6) / 1e6;
                        const std::string text = rounded == std::floor(rounded) ? std::to_string(static_cast<long long>(rounded))
                                                                                : std::format("{}", rounded);
                        std::vector<Token> number;
                        for (const char digit : text) number.push_back(character(std::string_view(&digit, 1), Catcodes::Category::Other));
                        made.push_back(std::move(number));
                    }
                }
                items.erase(items.begin() + static_cast<std::ptrdiff_t>(index));
                items.insert(items.begin() + static_cast<std::ptrdiff_t>(index), made.begin(), made.end());
                index += made.size();
            }

            // The body: a group, or the path up to its semicolon.
            blank();
            std::vector<Token> body;
            if (mouth.lookahead().is(Catcodes::Category::Group, '{')) {
                body = mouth.argument({}, 1);
            } else {
                for (Token token = mouth.read(); !token.empty(); token = mouth.read()) {
                    body.push_back(token);
                    if (token.is(';')) break;
                }
            }

            const Token define = character("\\define", Catcodes::Category::Escape);
            const Token open = character("{", Catcodes::Category::Group);
            const Token close = character("}", Catcodes::Category::Group);
            std::vector<Token> produced;
            std::size_t count = 0;
            for (const std::vector<Token>& item : items) {
                if (item.empty()) continue;
                ++count;
                produced.push_back(open);
                // Several variables take the item's parts between its `/`s.
                std::vector<std::vector<Token>> parts(1);
                for (const Token& token : item) {
                    if (variables.size() > 1 && token.is('/')) parts.emplace_back();
                    else parts.back().push_back(token);
                }
                for (std::size_t index = 0; index < variables.size(); ++index) {
                    produced.insert(produced.end(), {define, variables[index], open});
                    const std::vector<Token>& part = parts[std::min(index, parts.size() - 1)];
                    produced.insert(produced.end(), part.begin(), part.end());
                    produced.push_back(close);
                }
                if (!counter.empty()) {
                    produced.insert(produced.end(), {define, counter, open});
                    for (const char digit : std::to_string(count)) {
                        produced.push_back(character(std::string_view(&digit, 1), Catcodes::Category::Other));
                    }
                    produced.push_back(close);
                }
                produced.insert(produced.end(), body.begin(), body.end());
                produced.push_back(close);
                if (produced.size() > production) {
                    tracebacks.emplace_back(Traceback::Type::Memory, mouth.lookahead().location,
                                             std::format("\\foreach would produce past the limit of {} tokens", production));
                    return;
                }
            }
            mouth.stream().inject(std::span{produced});
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