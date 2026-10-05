/// @file
/// @brief Counter primitives: `\\newcounter`, `\\stepcounter`, `\\setcounter`
///        and their printed forms.
#include "render/primitives/counters.hpp"
#include "render/primitives/numeral.hpp"
#include "syntax/argument.hpp"
#include "syntax/number.hpp"
#include "logger.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace render::primitives {

    /// @brief Reads one argument, expanded, as an integer: `{2021}`,
    ///        `{\\value{section}}`, `{-1}`.
    /// @param mouth     Expander to read from.
    /// @param registers Bank a register reference may resolve against.
    /// @return The integer, or nothing when the argument does not read as one.
    static std::optional<std::int32_t> integer(syntax::Mouth& mouth,
                                               const syntax::semantics::Registers& registers) {
        const std::string expanded = syntax::Argument::expanded(mouth);
        mouth.ingest(mouth.arena.copy("{" + expanded + "}"));
        syntax::Cursor value(mouth.argument({}, 0));
        return syntax::Number::integer(value, registers);
    }

    Counters::Counters(syntax::Lexicon& lexicon) noexcept {
        lexicon.intern("\\newcounter");
        lexicon.intern("\\stepcounter");
        lexicon.intern("\\refstepcounter");
        lexicon.intern("\\setcounter");
        lexicon.intern("\\addtocounter");
        lexicon.intern("\\value");
        lexicon.intern("\\arabic");
        lexicon.intern("\\roman");
        lexicon.intern("\\Roman");
        lexicon.intern("\\alph");
        lexicon.intern("\\Alph");
        lexicon.intern("\\fnsymbol");
    }

    void Counters::spell(syntax::Mouth& mouth, const std::string_view name, const std::string_view parent,
                         const std::string_view numeral) {
        syntax::Lexicon& lexicon = mouth.lexicon;
        std::vector<syntax::Token> body;

        const auto word = [&](const std::string& text) {
            const syntax::Symbol bound = lexicon.intern(text);
            body.push_back(syntax::Token{bound, syntax::Catcodes::Category::Escape, memory::Location{},
                                         lexicon.resolve(bound)});
        };
        const auto character = [&](const char written, const syntax::Catcodes::Category category) {
            const syntax::Symbol bound = lexicon.intern(std::string_view(&written, 1));
            body.push_back(syntax::Token{bound, category, memory::Location{}, lexicon.resolve(bound)});
        };

        if (!parent.empty()) {
            word("\\the" + std::string(parent));
            character('.', syntax::Catcodes::Category::Other);
        }
        word(std::string(numeral));
        character('{', syntax::Catcodes::Category::Group);
        for (const char letter : name) {
            const bool alphabetic = (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z');
            character(letter, alphabetic ? syntax::Catcodes::Category::Letter : syntax::Catcodes::Category::Other);
        }
        character('}', syntax::Catcodes::Category::Group);

        // Global, as LaTeX's counters are: a counter made inside a group
        // still prints once the group is over.
        mouth.define(lexicon.intern("\\the" + std::string(name)), syntax::Mouth::Macro{.body = std::move(body)}, true);
    }

    void Counters::define(syntax::Mouth& mouth, const std::string_view name, const std::string_view parent,
                          const std::string_view numeral) const {
        const std::string key(name);
        Counter& counter = counters[key];

        if (counter.parent != parent) {
            if (!counter.parent.empty()) {
                std::erase(counters[counter.parent].children, key);
            }
            counter.parent = std::string(parent);
            if (!parent.empty()) {
                std::vector<std::string>& children = counters[std::string(parent)].children;
                if (!std::ranges::contains(children, key)) children.push_back(key);
            }
        }
        if (!numeral.empty()) spell(mouth, name, parent, numeral);
    }

    void Counters::reset(const std::string& name, const int depth) const {
        if (depth > 16) return;
        const auto found = counters.find(name);
        if (found == counters.end()) return;

        // Copied: resetting reaches other entries of the same table.
        const std::vector<std::string> children = found->second.children;
        for (const std::string& child : children) {
            counters[child].value = 0;
            reset(child, depth + 1);
        }
    }

    void Counters::step(const std::string_view name) const {
        const std::string key(name);
        ++counters[key].value;
        reset(key, 0);
    }

    void Counters::set(const std::string_view name, const int value) const {
        counters[std::string(name)].value = value;
    }

    int Counters::value(const std::string_view name) const {
        const auto found = counters.find(name);
        return found == counters.end() ? 0 : found->second.value;
    }

    bool Counters::contains(const std::string_view name) const {
        return counters.contains(name);
    }

    std::string Counters::print(syntax::Mouth& mouth, const std::string_view name) {
        mouth.ingest(mouth.arena.copy("{\\the" + std::string(name) + "}"));
        return syntax::Argument::expanded(mouth);
    }

    void Counters::operator()(syntax::Parser& parser, Context& context) const {

        parser.mouth.bind("\\newcounter", [this](syntax::Mouth& mouth) {
            const memory::Location origin = mouth.lookahead().location;
            const std::string name = syntax::Argument::text(mouth);

            // `[parent]`: the counter it is numbered within.
            std::string parent;
            for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                parent += token.text;
            }

            if (name.empty()) {
                tracebacks.emplace_back(syntax::Traceback::Type::Argument, origin,
                                         "\\newcounter needs a name");
                return;
            }
            if (counters.contains(name)) {
                tracebacks.emplace_back(syntax::Traceback::Type::Argument, origin,
                                         "Counter '" + name + "' already exists");
                return;
            }
            // Reset with its parent, but printed as a number of its own, as
            // LaTeX's \newcounter leaves it: `\numberwithin` is what prints
            // the parent's number in front.
            define(mouth, name, parent);
            if (!parent.empty()) spell(mouth, name, {}, "\\arabic");
        });

        // A known counter, or a report that there is none, for the commands
        // that change one.
        const auto known = [this](const std::string& name, const memory::Location origin) {
            if (counters.contains(name)) return true;
            tracebacks.emplace_back(syntax::Traceback::Type::Argument, origin, "No counter '" + name + "' defined");
            return false;
        };

        parser.mouth.bind("\\stepcounter", [this, known](syntax::Mouth& mouth) {
            const memory::Location origin = mouth.lookahead().location;
            const std::string name = syntax::Argument::text(mouth);
            if (known(name, origin)) step(name);
        });

        // Stepped, and made what a `\label` after it refers to.
        parser.mouth.bind("\\refstepcounter", [this, known, &context](syntax::Mouth& mouth) {
            const memory::Location origin = mouth.lookahead().location;
            const std::string name = syntax::Argument::text(mouth);
            if (!known(name, origin)) return;
            step(name);
            context.anchor = print(mouth, name);
            context.kind = name;
        });

        parser.mouth.bind("\\setcounter", [this, known, &context](syntax::Mouth& mouth) {
            const memory::Location origin = mouth.lookahead().location;
            const std::string name = syntax::Argument::text(mouth);
            // The page's number is the pages' own, known only once they are
            // made: set from the page this lands on, as \@folio says.
            if (name == "page") {
                if (const auto scanned = integer(mouth, context.registers); scanned && *scanned >= 0) {
                    mouth.ingest(context.arena.copy("\\@folio{" + std::to_string(*scanned) + "}"));
                }
                return;
            }
            const bool exists = known(name, origin);
            const auto scanned = integer(mouth, context.registers);
            if (!exists) return;
            if (!scanned) {
                tracebacks.emplace_back(syntax::Traceback::Type::Argument, origin,
                                         "\\setcounter needs a number");
                return;
            }
            set(name, *scanned);
        });

        parser.mouth.bind("\\addtocounter", [this, known, &context](syntax::Mouth& mouth) {
            const memory::Location origin = mouth.lookahead().location;
            const std::string name = syntax::Argument::text(mouth);
            const bool exists = known(name, origin);
            const auto scanned = integer(mouth, context.registers);
            if (!exists) return;
            if (!scanned) {
                tracebacks.emplace_back(syntax::Traceback::Type::Argument, origin,
                                         "\\addtocounter needs a number");
                return;
            }
            set(name, value(name) + *scanned);
        });

        // `\numberwithin{equation}{section}` and LaTeX's own spelling of it:
        // the first counter reset by the second and printed after it.
        // With a star, and as the kernel's \@addtoreset, it is only reset by
        // the other: its number prints as it did.
        const syntax::Mouth::Handler within = [this](syntax::Mouth& mouth) {
            const bool starred = mouth.lookahead().is('*');
            if (starred) mouth.read();
            const std::string name = syntax::Argument::text(mouth);
            const std::string parent = syntax::Argument::text(mouth);
            define(mouth, name, parent, starred ? "" : "\\arabic");
        };
        parser.mouth.bind("\\numberwithin", within);
        parser.mouth.bind("\\counterwithin", within);
        parser.mouth.bind("\\@addtoreset", [this](syntax::Mouth& mouth) {
            const std::string name = syntax::Argument::text(mouth);
            const std::string parent = syntax::Argument::text(mouth);
            define(mouth, name, parent, "");
        });
        parser.mouth.bind("\\counterwithout", [this](syntax::Mouth& mouth) {
            const std::string name = syntax::Argument::text(mouth);
            static_cast<void>(syntax::Argument::text(mouth));
            define(mouth, name, {});
        });

        // The printed forms, and the value itself. Each expands to its
        // characters, as LaTeX's do, so a number can be read where a
        // number is expected: `\ifnum\value{page}>1`.
        enum class Form { Arabic, Roman, Capitals, Letter, Uppercase, Symbol, Value };
        static constexpr std::array<std::pair<std::string_view, Form>, 7> forms{{
            {"\\arabic", Form::Arabic}, {"\\roman", Form::Roman}, {"\\Roman", Form::Capitals},
            {"\\alph", Form::Letter}, {"\\Alph", Form::Uppercase}, {"\\fnsymbol", Form::Symbol},
            {"\\value", Form::Value},
        }};

        for (const auto& [name, form] : forms) {
            parser.mouth.bind(name, [this, form](syntax::Mouth& mouth) {
                const memory::Location origin = mouth.lookahead().location;
                const std::string key = syntax::Argument::text(mouth);

                const auto found = counters.find(key);
                if (found == counters.end()) {
                    tracebacks.emplace_back(syntax::Traceback::Type::Argument, origin,
                                             "No counter '" + key + "' defined");
                    mouth.ingest("??");
                    return;
                }

                const int count = found->second.value;
                std::string text;
                switch (form) {
                    case Form::Arabic:
                    case Form::Value: text = Numeral::arabic(count); break;
                    case Form::Roman: text = Numeral::roman(count, false); break;
                    case Form::Capitals: text = Numeral::roman(count, true); break;
                    case Form::Letter: text = Numeral::alphabetic(count, false); break;
                    case Form::Uppercase: text = Numeral::alphabetic(count, true); break;
                    case Form::Symbol: {
                        // A footnote's asterisk, dagger and the rest, doubled
                        // once they run out, and digits past the last of them.
                        static constexpr std::array<std::string_view, 9> marks{
                            "*", "\xE2\x80\xA0", "\xE2\x80\xA1", "\xC2\xA7", "\xC2\xB6", "\xE2\x80\x96",
                            "**", "\xE2\x80\xA0\xE2\x80\xA0", "\xE2\x80\xA1\xE2\x80\xA1",
                        };
                        text = count >= 1 && static_cast<std::size_t>(count) <= marks.size()
                                   ? std::string(marks[static_cast<std::size_t>(count) - 1])
                                   : Numeral::arabic(count);
                        break;
                    }
                }
                if (!text.empty()) mouth.ingest(mouth.arena.copy(text));
            });
        }

        // A number in words, as fmtcount writes one: `\@numberwords{style}{n}`,
        // the style `number`, `Number` or `NUMBER` for the cardinal in lower
        // case, capitalised or in capitals, and `ordinal`, `Ordinal` or
        // `ORDINAL` for the ordinal the same three ways.
        parser.mouth.bind("\\@numberwords", [this](syntax::Mouth& mouth) {
            const memory::Location origin = mouth.lookahead().location;
            const std::string style = syntax::Argument::text(mouth);
            const std::string written = syntax::Argument::expanded(mouth);
            int value = 0;
            const auto [end, fault] = std::from_chars(written.data(), written.data() + written.size(), value);
            if (fault != std::errc{} || end != written.data() + written.size()) {
                tracebacks.emplace_back(syntax::Traceback::Type::Argument, origin,
                                         "A number is wanted in words, not '" + written + "'");
                return;
            }
            const bool ordinal = style == "ordinal" || style == "Ordinal" || style == "ORDINAL";
            std::string text = Numeral::words(value, ordinal);
            if (style == "Number" || style == "Ordinal") {
                text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
            } else if (style == "NUMBER" || style == "ORDINAL") {
                std::ranges::transform(text, text.begin(),
                                       [](const char letter) { return static_cast<char>(std::toupper(static_cast<unsigned char>(letter))); });
            }
            mouth.ingest(mouth.arena.copy(text));
        });

        // LaTeX's own counters, which the other modules step: equations,
        // floats and footnotes, each printed in Arabic numerals.
        for (const std::string_view name : {"equation", "figure", "table", "algorithm", "footnote"}) {
            define(parser.mouth, name);
        }
        // How deep the headings are numbered and listed, three levels as the
        // article class has it: a section, a subsection, a subsubsection.
        for (const std::string_view name : {"secnumdepth", "tocdepth"}) {
            define(parser.mouth, name);
            set(name, 3);
        }

        // subcaption's, lettered within the float they stand in and printed
        // as the letter alone: a reference puts the float's number before
        // it, and the caption parentheses round it.
        for (const auto& [name, parent] : {std::pair{"subfigure", "figure"}, std::pair{"subtable", "table"}}) {
            define(parser.mouth, name, parent, "\\alph");
            spell(parser.mouth, name, {}, "\\alph");
        }

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound counter primitives");
    }

}
