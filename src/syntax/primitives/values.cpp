/// @file
/// @brief Register primitives: `\\set`, `\\increase`, `\\reduce`, `\\scale`,
///        `\\name`, `\\the`, TeX's allocation and naming of registers, token
///        registers, e-TeX's expressions, `\\romannumeral` and `\\catcode`.
#include "syntax/primitives/values.hpp"
#include "syntax/number.hpp"
#include "syntax/semantics/union.hpp"
#include "logger.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <ctime>
#include <format>
#include <string>
#include <string_view>
#include <span>
#include <utility>

namespace syntax::primitives {

    Values::Values(Lexicon& lexicon) noexcept : lexicon(&lexicon) {
        relax = lexicon.intern("\\relax");
        toks = lexicon.intern("\\toks");
        numexpr = lexicon.intern("\\numexpr");
        dimexpr = lexicon.intern("\\dimexpr");
    }

    void Values::skip(Mouth& mouth, const std::string_view text) {
        while (mouth.lookahead().category == CatCodes::Category::Space) {
            mouth.read();
        }
        for (std::size_t index = 0; index < text.size(); ++index) {
            if (!mouth.lookahead(index).is(text[index])) return;
        }
        for (std::size_t index = 0; index < text.size(); ++index) {
            mouth.read();
        }
    }

    std::optional<semantics::Registers::Target> Values::slot(Mouth& mouth, Context& context) const {
        const Token lead = mouth.read();
        if (lead.category != CatCodes::Category::Escape) {
            tracebacks_.emplace_back(Traceback::Type::Register, lead.location,
                         "expected a register reference");
            return std::nullopt;
        }

        // A bound name addresses its slot directly: \set\tally = 7.
        if (const auto bound = context.registers.target(lead.symbol)) {
            return bound;
        }

        const auto kind = context.registers.bank(lead.symbol);
        if (!kind) {
            tracebacks_.emplace_back(Traceback::Type::Register, lead.location,
                         std::format("{} is not a register", lead.text));
            return std::nullopt;
        }

        const auto index = Number::integer(mouth, context.registers);
        if (!index || *index < 0 || *index >= static_cast<std::int32_t>(semantics::Registers::slots)) {
            tracebacks_.emplace_back(Traceback::Type::Register, lead.location,
                         std::format("{} needs a slot number between 0 and {}",
                                     lead.text, semantics::Registers::slots - 1));
            return std::nullopt;
        }

        return semantics::Registers::Target{*kind, static_cast<std::size_t>(*index)};
    }

    void Values::operator()(Mouth& mouth, Context& context) const {
        using Type = semantics::Registers::Type;

        // A bank is neither a macro nor a primitive, so a document cannot give
        // one a second name the way it renames anything else -- it is named
        // here instead. Both spellings of each reach the same storage, so
        // `\\count5` and `\\integer5` are one register and not two.
        if (lexicon) {
            context.registers.bind(lexicon->intern("\\integer"), Type::Count);
            context.registers.bind(lexicon->intern("\\count"), Type::Count);
            context.registers.bind(lexicon->intern("\\length"), Type::Dimension);
            context.registers.bind(lexicon->intern("\\dimen"), Type::Dimension);
            context.registers.bind(lexicon->intern("\\glue"), Type::Glue);
            context.registers.bind(lexicon->intern("\\skip"), Type::Glue);
            context.registers.bind(lexicon->intern("\\wd"), Type::Width);
            context.registers.bind(lexicon->intern("\\ht"), Type::Height);
            context.registers.bind(lexicon->intern("\\dp"), Type::Depth);

            // The date the run started, where it started -- a document dated
            // after eight in the evening in New York is not dated tomorrow --
            // kept as TeX keeps it: four integers a document reads as
            // `\\time` (minutes past midnight), `\\year`, `\\month` and
            // `\\day`. They sit at the top of the
            // integer bank, clear of the slots a document numbers from zero.
            const std::time_t now = std::time(nullptr);
            std::tm local{};
        #if defined(_WIN32)
            localtime_s(&local, &now);
        #else
            localtime_r(&now, &local);
        #endif
            const std::array<std::pair<std::string_view, std::int32_t>, 4> date{{
                {"\\time", static_cast<std::int32_t>(local.tm_hour * 60 + local.tm_min)},
                {"\\year", static_cast<std::int32_t>(local.tm_year + 1900)},
                {"\\month", static_cast<std::int32_t>(local.tm_mon + 1)},
                {"\\day", static_cast<std::int32_t>(local.tm_mday)},
            }};
            for (std::size_t index = 0; index < date.size(); ++index) {
                const std::size_t slot = semantics::Registers::slots - date.size() + index;
                context.registers.set(Type::Count, slot, date[index].second, true);
                context.registers.bind(lexicon->intern(date[index].first), Type::Count, slot);
            }
        }

        // Reading a value depends on the bank: an integer slot scans an
        // integer, a dimension or glue slot scans a dimension.
        const auto value = [this, &context](Mouth& mouth, const Type kind) -> std::optional<std::int32_t> {
            // A value may be produced by \evaluate or a macro; Number scans
            // rather than expands, so the stream is settled first.
            if (kind == Type::Count) {
                return Number::integer(mouth, context.registers);
            }

            // A glue register read whole, `\skip0 = \parskip`, brings what it
            // may stretch and shrink by with it.
            flex = {};
            if (const auto held = context.registers.target(mouth.lookahead().symbol);
                kind == Type::Glue && held && held->type == Type::Glue) {
                for (std::size_t part = 0; part < flex.size(); ++part) {
                    flex[part] = context.registers.get(static_cast<Type>(static_cast<std::size_t>(Type::Stretch) + part),
                                                       held->slot);
                }
            }
            const auto natural = Number::dimension(mouth, context.registers);

            // Glue keeps what it may stretch or shrink by beside its natural
            // width -- `plus 2pt minus 1pt`, or `plus 1fil` at an order of
            // infinity counted by the l's -- which an assignment keeps.
            if (kind == Type::Glue) {
                for (std::size_t part = 0; part < 2; ++part) {
                    const std::string_view keyword = part == 0 ? "plus" : "minus";
                    while (mouth.lookahead().category == CatCodes::Category::Space) mouth.read();
                    std::size_t matched = 0;
                    while (matched < keyword.size() && mouth.lookahead(matched).is(keyword[matched])) ++matched;
                    if (matched != keyword.size()) continue;
                    for (std::size_t index = 0; index < matched; ++index) mouth.read();
                    while (mouth.lookahead().category == CatCodes::Category::Space) mouth.read();

                    std::vector<Token> read;
                    std::string amount;
                    for (Token next = mouth.lookahead(); next.text.size() == 1 &&
                                                         (next.category == CatCodes::Category::Letter ||
                                                          (next.text[0] >= '0' && next.text[0] <= '9') ||
                                                          next.is('.') || next.is(',') || next.is('-'));
                         next = mouth.lookahead()) {
                        read.push_back(mouth.read());
                        amount += next.text;
                    }
                    const std::size_t infinite = amount.find("fil");
                    if (infinite == std::string::npos) {
                        mouth.stream().inject(std::span{read});
                        flex[part] = Number::dimension(mouth, context.registers).value_or(0);
                        flex[2] &= part == 0 ? ~3 : 3;
                        continue;
                    }
                    double weight = 1.0;
                    if (infinite > 0) std::from_chars(amount.data(), amount.data() + infinite, weight);
                    const auto order = static_cast<std::int32_t>(std::min<std::size_t>(amount.size() - infinite - 2, 3));
                    flex[part] = static_cast<std::int32_t>(weight * 65536.0);
                    flex[2] = part == 0 ? (flex[2] & ~3) | order : (flex[2] & 3) | order << 2;
                }
            }
            return natural;
        };

        // e-TeX's expressions: `\numexpr 2*\count0+1\relax`, and
        // `\dimexpr \textwidth-2\parskip\relax` for lengths -- which is also
        // what the calc package lets \setlength be written as. Read up to the
        // \relax, expanded as they go, then worked out with the usual
        // precedence: * and / by a whole number before + and -, brackets
        // first. Division rounds, as e-TeX's does. A length in the answer is
        // in scaled points.
        const auto calculate = [this, &context](Mouth& mouth, const bool dimensional) -> std::optional<std::int32_t> {
            std::vector<Token> written;
            // \relax is looked for before anything expands: it is a primitive
            // here, and expanding it would read past it as though it were not.
            // So the expression is expanded a step at a time, and the \relax
            // found wherever it comes to stand -- after a conditional's \fi as
            // much as at the front. A macro that only ever gives itself back
            // is cut off, and ends the expression.
            for (std::size_t steps = 0;; ++steps) {
                if (mouth.lookahead().symbol == relax) {
                    mouth.read();
                    break;
                }
                if (steps < 10000 && mouth.step()) continue;
                const Token token = mouth.read();
                if (token.empty()) break;
                // `plus` and `minus` end a length's expression: they are glue's.
                if (dimensional && token.category == CatCodes::Category::Letter && (token.is('p') || token.is('m'))) {
                    const std::string_view rest = token.is('p') ? "lus" : "inus";
                    bool keyword = true;
                    for (std::size_t index = 0; index < rest.size(); ++index) {
                        keyword = keyword && mouth.lookahead(index).is(rest[index]);
                    }
                    if (keyword) {
                        mouth.stream().inject(std::span{&token, 1});
                        break;
                    }
                }
                // A command that is neither a register nor anything a number
                // is made of ends the expression, and is read again after it.
                const bool counted = context.registers.bank(token.symbol) || context.registers.target(token.symbol);
                if ((token.category == CatCodes::Category::Escape && !counted) ||
                    token.category == CatCodes::Category::Group) {
                    mouth.stream().inject(std::span{&token, 1});
                    break;
                }
                written.push_back(token);
            }

            Cursor stream(std::move(written));
            bool sound = true;
            const auto blanks = [&stream] {
                while (!stream.empty() && stream.lookahead().category == CatCodes::Category::Space) stream.advance();
            };
            // A sum of terms, a term a product of factors, a factor a signed
            // quantity or a bracketed sum. A length times or over a number
            // is a length; what multiplies or divides is always a number.
            const auto sum = [&](this const auto& self, const bool length, const bool bracketed) -> std::int64_t {
                const auto factor = [&](const bool measured) -> std::int64_t {
                    blanks();
                    const bool negative = stream.lookahead().is('-');
                    if (negative || stream.lookahead().is('+')) {
                        stream.advance();
                        blanks();
                    }
                    std::int64_t value = 0;
                    if (stream.lookahead().is('(')) {
                        stream.advance();
                        value = self(measured, true);
                    } else if (const auto scanned = measured ? Number::dimension(stream, context.registers)
                                                             : Number::integer(stream, context.registers)) {
                        value = *scanned;
                    } else {
                        sound = false;
                    }
                    return negative ? -value : value;
                };
                const auto term = [&]() -> std::int64_t {
                    std::int64_t value = factor(length);
                    while (true) {
                        blanks();
                        const bool times = stream.lookahead().is('*');
                        if (!times && !stream.lookahead().is('/')) return value;
                        stream.advance();
                        const std::int64_t amount = factor(false);
                        if (times) {
                            value *= amount;
                        } else if (amount != 0) {
                            // Rounded half away from zero, as e-TeX divides.
                            const std::int64_t half = (value < 0) == (amount < 0) ? amount : -amount;
                            value = (2 * value + half) / (2 * amount);
                        }
                    }
                };
                std::int64_t total = term();
                while (true) {
                    blanks();
                    if (bracketed && stream.lookahead().is(')')) {
                        stream.advance();
                        return total;
                    }
                    const bool plus = stream.lookahead().is('+');
                    if (!plus && !stream.lookahead().is('-')) return total;
                    stream.advance();
                    total += plus ? term() : -term();
                }
            };
            const std::int64_t value = sum(dimensional, false);
            blanks();
            if (!sound || !stream.empty()) {
                tracebacks_.emplace_back(Traceback::Type::Syntax, mouth.lookahead().location,
                              dimensional ? "\\dimexpr could not read its expression"
                                          : "\\numexpr could not read its expression");
            }
            return static_cast<std::int32_t>(std::clamp<std::int64_t>(value, -2147483647, 2147483647));
        };

        for (const bool dimensional : {false, true}) {
            mouth.bind(dimensional ? "\\dimexpr" : "\\numexpr", [calculate, dimensional](Mouth& mouth) {
                const std::int32_t value = calculate(mouth, dimensional).value_or(0);
                mouth.ingest(mouth.arena().copy(dimensional ? std::format("{}sp ", value) : std::format("{} ", value)));
            });
        }
        mouth.bind("\\glueexpr", [calculate](Mouth& mouth) {
            mouth.ingest(mouth.arena().copy(std::format("{}sp ", calculate(mouth, true).value_or(0))));
        });

        // The document's own registers, given out one at a time: plain TeX's
        // \newcount, \newdimen, \newskip, LaTeX's \newlength over them, and
        // \newtoks and \newbox. Each counts up from 128 in its own bank, clear
        // of the low slots a document numbers for itself and of the engine's
        // own from 240. The name is written bare or, as LaTeX writes it, braced.
        const auto named = [this](Mouth& mouth, const std::string_view primitive) -> Token {
            Token name = mouth.read();
            while (name.category == CatCodes::Category::Space) name = mouth.read();
            if (name.is(CatCodes::Category::Group, '{')) {
                name = mouth.read();
                if (!mouth.read().is(CatCodes::Category::Group, '}')) name = {};
            }
            if (name.category != CatCodes::Category::Escape) {
                tracebacks_.emplace_back(Traceback::Type::Register, name.location,
                              std::format("{} needs a control sequence to name", primitive));
                return {};
            }
            return name;
        };
        const auto allocate = [this, &context](const Token& name, const Type type) -> std::optional<std::size_t> {
            std::size_t& next = following[static_cast<std::size_t>(type)];
            if (next >= semantics::Registers::reserved) {
                tracebacks_.emplace_back(Traceback::Type::Register, name.location,
                              std::format("No {} register is left for {}", type == Type::Count ? "integer" : "length",
                                          name.text));
                return std::nullopt;
            }
            context.registers.set(type, next, 0, true);
            context.registers.bind(name.symbol, type, next);
            return next++;
        };

        for (const auto& [primitive, type] : {std::pair{"\\newcount", Type::Count}, std::pair{"\\newdimen", Type::Dimension},
                                              std::pair{"\\newlength", Type::Dimension}, std::pair{"\\newskip", Type::Glue},
                                              std::pair{"\\newmuskip", Type::Glue}}) {
            mouth.bind(primitive, [named, allocate, primitive, type](Mouth& mouth) {
                if (const Token name = named(mouth, primitive); !name.empty()) static_cast<void>(allocate(name, type));
            });
        }

        // \chardef and \mathchardef: a name for a number, kept in an integer
        // register of its own so it reads as the number anywhere one is read.
        for (const std::string_view primitive : {"\\chardef", "\\mathchardef"}) {
            mouth.bind(primitive, [&context, named, allocate, primitive](Mouth& mouth) {
                const Token name = named(mouth, primitive);
                if (name.empty()) return;
                skip(mouth, "=");
                const std::int32_t value = Number::integer(mouth, context.registers).value_or(0);
                if (const auto slot = allocate(name, Type::Count)) context.registers.set(Type::Count, *slot, value, true);
            });
        }

        // \countdef, \dimendef and \skipdef: a name for a register a document
        // numbers itself -- `\countdef\tally=5`.
        for (const auto& [primitive, type] : {std::pair{"\\countdef", Type::Count}, std::pair{"\\dimendef", Type::Dimension},
                                              std::pair{"\\skipdef", Type::Glue}}) {
            mouth.bind(primitive, [this, &context, named, primitive, type](Mouth& mouth) {
                const Token name = named(mouth, primitive);
                if (name.empty()) return;
                skip(mouth, "=");
                const auto index = Number::integer(mouth, context.registers);
                if (!index || *index < 0 || *index >= static_cast<std::int32_t>(semantics::Registers::slots)) {
                    tracebacks_.emplace_back(Traceback::Type::Register, name.location,
                                  std::format("{} needs a register number between 0 and 255", primitive));
                    return;
                }
                context.registers.bind(name.symbol, type, static_cast<std::size_t>(*index));
            });
        }

        // \newbox: a box register's number, named as \chardef names one; the
        // boxes themselves are the render layer's.
        mouth.bind("\\newbox", [this, &context, named, allocate](Mouth& mouth) {
            const Token name = named(mouth, "\\newbox");
            if (name.empty()) return;
            if (const auto slot = allocate(name, Type::Count)) {
                context.registers.set(Type::Count, *slot, static_cast<std::int32_t>(boxes), true);
                boxes = std::min<std::size_t>(boxes + 1, semantics::Registers::slots - 1);
            }
        });

        // Token registers: `\toks0={...}`, or a name \newtoks or \toksdef
        // gave one, assigned the same way and read back with \the. A name is
        // itself the assignment, as it is in TeX.
        const auto store = [this](Mouth& mouth, const std::size_t slot) {
            skip(mouth, "=");
            while (mouth.lookahead().category == CatCodes::Category::Space) mouth.read();
            lists[slot] = mouth.argument({}, 1);
        };
        const auto list = [this, store](Mouth& mouth, const Token& name, const std::size_t slot) {
            if (name.symbol >= listed.size()) listed.resize(std::max<std::size_t>(name.symbol + 1, listed.size() * 2), 0);
            listed[name.symbol] = static_cast<std::uint16_t>(slot + 1);
            mouth.bind(name.symbol, [store, slot](Mouth& mouth) { store(mouth, slot); });
        };
        mouth.bind("\\toks", [&context, store](Mouth& mouth) {
            const auto index = Number::integer(mouth, context.registers);
            if (index && *index >= 0 && *index < static_cast<std::int32_t>(semantics::Registers::slots)) {
                store(mouth, static_cast<std::size_t>(*index));
            }
        });
        mouth.bind("\\newtoks", [this, named, list](Mouth& mouth) {
            const Token name = named(mouth, "\\newtoks");
            if (name.empty() || tokens >= semantics::Registers::reserved) return;
            list(mouth, name, tokens++);
        });
        mouth.bind("\\toksdef", [&context, named, list](Mouth& mouth) {
            const Token name = named(mouth, "\\toksdef");
            if (name.empty()) return;
            skip(mouth, "=");
            const auto index = Number::integer(mouth, context.registers);
            if (index && *index >= 0 && *index < static_cast<std::int32_t>(semantics::Registers::slots)) {
                list(mouth, name, static_cast<std::size_t>(*index));
            }
        });

        // TeX's \number: a number as its digits -- a register, a \chardef,
        // an expression -- where \evaluate works one out from a group.
        mouth.bind("\\number", [&context](Mouth& mouth) {
            mouth.ingest(mouth.arena().copy(std::format("{}", Number::integer(mouth, context.registers).value_or(0))));
        });

        // TeX's \char: the character a number names, set as itself --
        // `\char"2713` a check mark, `\char92` a backslash rather than the
        // start of a command, since each of TeX's special characters comes
        // back as the command that prints it.
        mouth.bind("\\char", [&context](Mouth& mouth) {
            const auto point = static_cast<std::uint32_t>(
                std::clamp(Number::integer(mouth, context.registers).value_or(0), 0, 0x10FFFF));
            std::string written;
            switch (point) {
                case '\\': written = "\\textbackslash "; break;
                case '{': written = "\\{"; break;
                case '}': written = "\\}"; break;
                case '^': written = "\\textasciicircum "; break;
                case '~': written = "\\textasciitilde "; break;
                case '#': case '$': case '%': case '&': case '_':
                    written = {'\\', static_cast<char>(point)};
                    break;
                default:
                    if (point < 0x80) {
                        written += static_cast<char>(point);
                    } else if (point < 0x800) {
                        written += static_cast<char>(0xC0 | (point >> 6));
                        written += static_cast<char>(0x80 | (point & 0x3F));
                    } else if (point < 0x10000) {
                        written += static_cast<char>(0xE0 | (point >> 12));
                        written += static_cast<char>(0x80 | ((point >> 6) & 0x3F));
                        written += static_cast<char>(0x80 | (point & 0x3F));
                    } else {
                        written += static_cast<char>(0xF0 | (point >> 18));
                        written += static_cast<char>(0x80 | ((point >> 12) & 0x3F));
                        written += static_cast<char>(0x80 | ((point >> 6) & 0x3F));
                        written += static_cast<char>(0x80 | (point & 0x3F));
                    }
            }
            if (point != 0) mouth.ingest(mouth.arena().copy(written));
        });

        // \romannumeral: a number in lower-case Roman numerals, and nothing
        // at all for one that is not positive -- which is why TeX's own
        // macros use it to expand what follows it and leave nothing behind.
        mouth.bind("\\romannumeral", [&context](Mouth& mouth) {
            std::int32_t value = Number::integer(mouth, context.registers).value_or(0);
            static constexpr std::array<std::pair<std::int32_t, std::string_view>, 13> numerals{{
                {1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"}, {90, "xc"}, {50, "l"},
                {40, "xl"}, {10, "x"}, {9, "ix"}, {5, "v"}, {4, "iv"}, {1, "i"},
            }};
            std::string written;
            for (const auto& [amount, letters] : numerals) {
                for (; value >= amount; value -= amount) written += letters;
            }
            if (!written.empty()) mouth.ingest(mouth.arena().copy(written));
        });

        // \catcode: what a character means to the lexer from here on --
        // `\catcode`\@=11`. \lccode, \uccode, \sfcode, \mathcode and
        // \delcode are read and let go: case changes and spacing here follow
        // Unicode rather than a table a document keeps.
        mouth.bind("\\catcode", [this, &context](Mouth& mouth) {
            const auto code = Number::integer(mouth, context.registers);
            skip(mouth, "=");
            const auto category = Number::integer(mouth, context.registers);
            if (!code || !category || *code < 0 || *code > 255 || *category < 0 || *category > 15) {
                tracebacks_.emplace_back(Traceback::Type::Register, mouth.lookahead().location,
                              "\\catcode needs a character and a category between 0 and 15");
                return;
            }
            // TeX's numbering, onto the lexer's categories: 9 is ignored, 14 a
            // comment, 15 invalid.
            static constexpr std::array<CatCodes::Category, 16> categories{
                CatCodes::Category::Escape, CatCodes::Category::Group, CatCodes::Category::Group,
                CatCodes::Category::Shift, CatCodes::Category::Align, CatCodes::Category::Space,
                CatCodes::Category::Parameter, CatCodes::Category::Mark, CatCodes::Category::Index,
                CatCodes::Category::Ignore, CatCodes::Category::Space, CatCodes::Category::Letter,
                CatCodes::Category::Other, CatCodes::Category::Active, CatCodes::Category::Comment,
                CatCodes::Category::Invalid,
            };
            const bool global = std::exchange(context.global, false);
            const CatCodes::Category chosen = categories[static_cast<std::size_t>(*category)];
            mouth.state().catcodes().set(static_cast<char>(*code), chosen, global);
            // What the document has not yet been read of is read the new way.
            mouth.recategorize(static_cast<char>(*code), chosen, global);
        });
        for (const std::string_view primitive : {"\\lccode", "\\uccode", "\\sfcode", "\\mathcode", "\\delcode"}) {
            mouth.bind(primitive, [&context](Mouth& mouth) {
                static_cast<void>(Number::integer(mouth, context.registers));
                skip(mouth, "=");
                static_cast<void>(Number::integer(mouth, context.registers));
            });
        }

        // TeX's \the: what a register holds, as text to be read on -- an
        // integer's digits, or a length in points printed as TeX prints
        // one, with the fewest decimals that still read back to the same
        // scaled points: `12.0pt`, `0.4pt`, `3.14159pt`.
        mouth.bind("\\the", [this, &context, &mouth, calculate](Mouth&) {
            const Token lead = mouth.lookahead();

            // A token list: what it holds, as it was given.
            const bool indexed = lead.symbol == toks;
            if (indexed || (lead.symbol < listed.size() && listed[lead.symbol] != 0)) {
                mouth.read();
                std::size_t index = indexed ? 0 : listed[lead.symbol] - 1uz;
                if (indexed) {
                    const auto scanned = Number::integer(mouth, context.registers);
                    if (!scanned || *scanned < 0 || *scanned >= static_cast<std::int32_t>(semantics::Registers::slots)) return;
                    index = static_cast<std::size_t>(*scanned);
                }
                if (!lists[index].empty()) mouth.stream().inject(std::span{lists[index]});
                return;
            }

            // An expression, printed as a register holding its value would be.
            std::optional<semantics::Registers::Target> target;
            std::int32_t held = 0;
            if (lead.symbol == numexpr || lead.symbol == dimexpr) {
                mouth.read();
                held = calculate(mouth, lead.symbol == dimexpr).value_or(0);
                target = semantics::Registers::Target{lead.symbol == dimexpr ? Type::Dimension : Type::Count, 0};
            } else {
                target = slot(mouth, context);
                if (!target) return;
                held = context.registers.get(target->type, target->slot);
            }

            std::string text;
            const auto print = [&text](std::int32_t amount, const std::string_view unit) {
                constexpr std::int32_t unity = 65536;
                if (amount < 0) {
                    text += '-';
                    amount = -amount;
                }
                text += std::format("{}.", amount / unity);
                std::int32_t rest = 10 * (amount % unity) + 5;
                std::int32_t delta = 10;
                do {
                    if (delta > unity) rest += 0x8000 - 50000;   // round the last digit
                    text += static_cast<char>('0' + rest / unity);
                    rest = 10 * (rest % unity);
                    delta *= 10;
                } while (rest > delta);
                text += unit;
            };
            if (target->type == Type::Count) {
                text = std::format("{}", held);
            } else {
                print(held, "pt");
            }

            // Glue with what it may stretch and shrink by, as TeX prints it:
            // `4.0pt plus 1.0fil minus 2.0pt`.
            if (target->type == Type::Glue) {
                static constexpr std::array<std::string_view, 4> units{"pt", "fil", "fill", "filll"};
                const std::int32_t orders = context.registers.get(Type::Order, target->slot);
                for (std::size_t part = 0; part < 2; ++part) {
                    const std::int32_t side = context.registers.get(
                        static_cast<Type>(static_cast<std::size_t>(Type::Stretch) + part), target->slot);
                    if (side == 0) continue;
                    text += part == 0 ? " plus " : " minus ";
                    print(side, units[static_cast<std::size_t>((orders >> (part * 2)) & 3)]);
                }
            }
            mouth.ingest(mouth.arena().copy(text));
        });

        mouth.bind("\\set", [this, &context, value, &mouth](Mouth&) {
            const auto target = slot(mouth, context);
            if (!target) return;

            skip(mouth, "=");
            const auto scanned = value(mouth, target->type);
            if (!scanned) {
                tracebacks_.emplace_back(Traceback::Type::Register, mouth.lookahead().location,
                              "\\set needs a value");
                return;
            }

            const bool global = std::exchange(context.global, false);
            context.registers.set(target->type, target->slot, *scanned, global);
            if (target->type == Type::Glue) {
                for (std::size_t part = 0; part < flex.size(); ++part) {
                    context.registers.set(static_cast<Type>(static_cast<std::size_t>(Type::Stretch) + part),
                                          target->slot, flex[part], global);
                }
            }
            if (!context.after.empty()) {
                const Token after = std::exchange(context.after, Token{});
                mouth.stream().inject(std::span{&after, 1});
            }
        });

        // The three arithmetic primitives differ only in the operation, so
        // they share one body. Arithmetic is done in std::int64_t and clamped,
        // because a register holds exactly 32 bits.
        const auto modify = [this, &context, value](Mouth& mouth, const char operation) {
            const auto target = slot(mouth, context);
            if (!target) return;

            // Added to, a register takes what it holds -- a length to a
            // length; multiplied or divided, it takes a whole number, as
            // TeX's \multiply and \divide do whatever the register is.
            skip(mouth, "by");
            const auto scanned = operation == '+' ? value(mouth, target->type)
                                                  : Number::integer(mouth, context.registers);
            if (!scanned) {
                tracebacks_.emplace_back(Traceback::Type::Register, mouth.lookahead().location,
                              "expected an amount");
                return;
            }

            const auto current = static_cast<std::int64_t>(
                context.registers.get(target->type, target->slot));
            const auto amount = static_cast<std::int64_t>(*scanned);

            std::int64_t result = current;
            switch (operation) {
                case '+': result = current + amount; break;
                case '*': result = current * amount; break;
                case '/':
                    if (amount == 0) {
                        tracebacks_.emplace_back(Traceback::Type::Register, mouth.lookahead().location,
                                      "\\reduce by zero");
                        return;
                    }
                    result = current / amount;
                    break;
                default: break;
            }

            constexpr std::int64_t ceiling = 2147483647;
            if (result > ceiling || result < -ceiling) {
                tracebacks_.emplace_back(Traceback::Type::Register, mouth.lookahead().location,
                              std::format("register arithmetic overflowed ({})", result));
                result = result > 0 ? ceiling : -ceiling;
            }

            const bool global = std::exchange(context.global, false);
            context.registers.set(target->type, target->slot, static_cast<std::int32_t>(result), global);

            // Glue's stretch and shrink go with its width: added to, each
            // side keeps the higher order of the two and adds at an equal
            // one, as TeX's \advance does; multiplied or divided, each is.
            if (target->type == Type::Glue) {
                std::int32_t orders = context.registers.get(Type::Order, target->slot);
                for (std::size_t part = 0; part < 2; ++part) {
                    const auto bank = static_cast<Type>(static_cast<std::size_t>(Type::Stretch) + part);
                    const int shift = static_cast<int>(part) * 2;
                    const std::int32_t mine = (orders >> shift) & 3;
                    std::int64_t side = context.registers.get(bank, target->slot);
                    if (operation == '+') {
                        const std::int32_t theirs = (flex[2] >> shift) & 3;
                        if (theirs == mine) {
                            side += flex[part];
                        } else if (theirs > mine && flex[part] != 0) {
                            side = flex[part];
                            orders = (orders & ~(3 << shift)) | theirs << shift;
                        }
                    } else {
                        side = operation == '*' ? side * amount : side / amount;
                    }
                    context.registers.set(bank, target->slot,
                                          static_cast<std::int32_t>(std::clamp<std::int64_t>(side, -ceiling, ceiling)),
                                          global);
                }
                context.registers.set(Type::Order, target->slot, orders, global);
            }
            if (!context.after.empty()) {
                const Token after = std::exchange(context.after, Token{});
                mouth.stream().inject(std::span{&after, 1});
            }
        };

        mouth.bind("\\increase", [modify, &mouth](Mouth&) { modify(mouth, '+'); });
        mouth.bind("\\scale",    [modify, &mouth](Mouth&) { modify(mouth, '*'); });
        mouth.bind("\\reduce",   [modify, &mouth](Mouth&) { modify(mouth, '/'); });

        mouth.bind("\\name", [this, &context, &mouth](Mouth&) {
            const Token alias = mouth.read();
            if (alias.category != CatCodes::Category::Escape) {
                tracebacks_.emplace_back(Traceback::Type::Register, alias.location,
                              "\\name needs a control sequence");
                return;
            }

            const auto target = slot(mouth, context);
            if (!target) return;

            context.registers.bind(alias.symbol, target->type, target->slot);
        });

        Logger::log(Logger::Type::Semantics, Logger::Level::Debug, "Bound register primitives");
    }

}