/// @file
/// @brief Definition primitives: `\\define`, `\\declare`, `\\switch`,
///        `\\forget`, `\\alias`, the prefixes, and the names built or
///        expanded out of order: `\\csname` and `\\expandafter`.
#include "syntax/primitives/macros.hpp"
#include "syntax/number.hpp"
#include "logger.hpp"

#include <array>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace syntax::primitives {

    Macros::Macros(Lexicon& lexicon) noexcept {
        integer = lexicon.intern("\\integer");
        alias = lexicon.intern("\\@alias");
        truth = lexicon.intern("\\iftrue");
        falsehood = lexicon.intern("\\iffalse");
        terminate = lexicon.intern("\\endcsname");
        relax = lexicon.intern("\\relax");
        // No document can write this name: a colon ends a control word.
        finish = lexicon.intern("\\define:end");
    }

    void Macros::operator()(Mouth& mouth, Context& context) const {
        Lexicon& lexicon = mouth.lexicon;

        // Prefixes. Each sets a flag the next definition takes, so they
        // compose and may be written in any order.
        mouth.bind("\\shared",   [&context](Mouth&) { context.global = true; });
        mouth.bind("\\spanning", [&context](Mouth&) { context.spanning = true; });
        mouth.bind("\\guarded",  [&context](Mouth&) { context.isolated = true; });
        mouth.bind("\\provided", [&context](Mouth&) { context.provided = true; });

        // Tokens as they come out of the expander, up to the end of the ones
        // given: what `\\edef` stores and e-TeX's `\\expanded` puts back. The
        // end is marked with a name nothing means, which expand() therefore
        // hands back as it stands.
        const auto flatten = [this, &mouth, &lexicon](const std::vector<Token>& tokens) {
            const Token end{finish, Catcodes::Category::Escape, {}, lexicon.resolve(finish)};
            mouth.stream().inject(std::span{&end, 1});
            mouth.stream().inject(std::span{tokens});

            ++flattening;
            const bool guarded = mouth.robust(true);
            std::vector<Token> written;
            written.reserve(tokens.size());
            for (Token token = mouth.expand(); !token.empty() && token.symbol != finish; token = mouth.expand()) {
                // A name \noexpand or \unexpanded held back comes out under
                // no symbol, which is what kept it from expanding; kept, it is
                // itself again.
                if (token.symbol == none && !token.text.empty() &&
                    (token.category == Catcodes::Category::Escape || token.category == Catcodes::Category::Active)) {
                    token.symbol = lexicon.intern(token.text);
                }
                written.push_back(token);
            }
            mouth.robust(guarded);
            --flattening;
            return written;
        };

        // A prefix before a definition, as `\\edef` is `\\expanded\\define`;
        // before a brace group, e-TeX's own `\\expanded{...}`: the group,
        // expanded through and put back.
        mouth.bind("\\expanded", [&context, &mouth, flatten](Mouth&) {
            if (!mouth.lookahead().is(Catcodes::Category::Group, '{')) {
                context.expanded = true;
                return;
            }
            const std::vector<Token> written = flatten(mouth.argument({}, 1));
            mouth.stream().inject(std::span{written});
        });

        // The name a definition makes. LaTeX writes it in braces --
        // `\newcommand{\foo}` -- and TeX writes it bare; both name the same
        // macro. LaTeX's star forbids a paragraph break in the arguments,
        // which the definition reads for itself before this; here it is only
        // stepped over. An empty token when there is none.
        const auto named = [this, &mouth](std::string_view primitive) -> Token {
            Token name = mouth.read();
            if (name.is(Catcodes::Category::Other, '*')) name = mouth.read();
            if (name.is(Catcodes::Category::Group, '{')) {
                const Token inner = mouth.read();
                if (!mouth.read().is(Catcodes::Category::Group, '}')) {
                    tracebacks.emplace_back(Traceback::Type::Macro, name.location,
                                  std::format("{} needs one control sequence in its braces", primitive));
                    return {};
                }
                name = inner;
            }
            if (name.category != Catcodes::Category::Escape && name.category != Catcodes::Category::Active) {
                tracebacks.emplace_back(Traceback::Type::Macro, name.location,
                              std::format("{} needs a control sequence to name", primitive));
                return {};
            }
            return name;
        };

        mouth.bind("\\define", [this, &context, &mouth, flatten, named](Mouth&) {
            // The prefixes are taken first, so that none can leak onto
            // whatever the body defines as it is expanded, or onto the next
            // definition when this one is abandoned.
            const bool global = std::exchange(context.global, false);
            // LaTeX's star keeps a paragraph out of the arguments of a
            // command that would otherwise take one: `\newcommand*`.
            const bool spanning = std::exchange(context.spanning, false) && !mouth.lookahead().is('*');
            const bool isolated = std::exchange(context.isolated, false);
            const bool provided = std::exchange(context.provided, false);
            const bool expanded = std::exchange(context.expanded, false);
            const bool robust = std::exchange(context.robust, false);

            const Token name = named("\\define");
            if (name.empty()) return;

            // How many parameters, written either way. LaTeX counts them in
            // brackets and TeX lists them out, and a document may use either
            // because both say the same thing: the body's #1..#n only mean
            // anything once the count is known, and that is all either form
            // is for here.
            int declared = 0;
            bool optional = false;          // the first parameter may be left out
            std::vector<Token> fallback;    // and is this when it is

            // A bracket before a parameter mark is plain TeX's parameter
            // text, `\def\x[#1]{...}`, not a count.
            if (mouth.lookahead().is('[') && !mouth.lookahead(1).is('#')) {
                mouth.read();
                const auto scanned = Number::integer(mouth, context.registers);
                if (!scanned || *scanned < 0 || *scanned > parameters) {
                    tracebacks.emplace_back(Traceback::Type::Argument, name.location,
                                  std::format("\\define{} takes between 0 and {} parameters",
                                              name.text, parameters));
                    return;
                }
                declared = *scanned;
                if (!mouth.lookahead().is(']')) {
                    tracebacks.emplace_back(Traceback::Type::Argument, name.location,
                                  "\\define parameter count is missing its ']'");
                    return;
                }
                mouth.read();

                // `[n][default]`: LaTeX's optional first argument, written in
                // brackets when it is given, and what it stands for when not.
                if (declared > 0 && mouth.lookahead().is('[')) {
                    fallback = mouth.argument(Mouth::Parameter{.optional = true}, 0);
                    optional = true;
                }
            }

            // `#1,#2;...` up to the brace that opens the body. The numbers
            // run in order from one: this is a count written out, not a
            // pattern to be matched against. Whatever stands after one #N
            // and before the next -- or before the body's own `{` -- is that
            // parameter's delimiter, exactly as plain TeX's `\def` reads one;
            // left empty, a parameter reads one brace group as it always did.
            // A `[n]` count above never delimits, since that spelling is
            // LaTeX's own and LaTeX's arguments are always braced.
            std::vector<std::vector<Token>> delimiters;
            std::vector<Token> prefix;

            if (!mouth.lookahead().is(Catcodes::Category::Group, '{')) {
                // What stands before the first parameter, matched where the
                // macro is used.
                while (!mouth.lookahead().empty() && !mouth.lookahead().is('#') &&
                       !mouth.lookahead().is(Catcodes::Category::Group, '{')) {
                    prefix.push_back(mouth.read());
                }
                while (mouth.lookahead().is('#')) {
                    mouth.read();

                    const Token digit = mouth.read();
                    if (digit.text.size() != 1 ||
                        digit.text[0] != static_cast<char>('1' + declared)) {
                        tracebacks.emplace_back(
                            Traceback::Type::Argument, name.location,
                            std::format("\\define{} expects #{} here", name.text, declared + 1));
                        return;
                    }

                    if (++declared > parameters) {
                        tracebacks.emplace_back(
                            Traceback::Type::Argument, name.location,
                            std::format("\\define{} takes at most {} parameters",
                                        name.text, parameters));
                        return;
                    }

                    std::vector<Token> delimiter;
                    while (!mouth.lookahead().is('#') &&
                           !mouth.lookahead().is(Catcodes::Category::Group, '{')) {
                        const Token next = mouth.read();
                        if (next.empty()) break;
                        delimiter.push_back(next);
                    }
                    delimiters.push_back(std::move(delimiter));
                }
            }

            Mouth::Macro macro;
            macro.parameters.resize(static_cast<std::size_t>(declared));
            macro.prefix = std::move(prefix);
            for (std::size_t index = 0; index < delimiters.size(); ++index) {
                macro.parameters[index].delimiters = std::move(delimiters[index]);
            }
            if (optional) {
                macro.parameters[0].optional = true;
                macro.parameters[0].fallbacks = std::move(fallback);
            }
            macro.spanning = spanning;
            macro.isolated = isolated;
            macro.robust = robust;

            // A body may hold `\\par` whatever the flags say: `\\long` is about
            // the arguments a macro is later called with, never about what it
            // is defined to be. A macro that sets a paragraph break is common.
            macro.body = mouth.argument({}, 1);

            // LaTeX's \providecommand reads the whole definition either way,
            // and keeps it only when the name is still free.
            if (provided && mouth.known(name.symbol)) return;
            if (expanded) macro.body = flatten(macro.body);

            mouth.define(name.symbol, std::move(macro), global);
            if (!context.after.empty()) {
                const Token after = std::exchange(context.after, Token{});
                mouth.stream().inject(std::span{&after, 1});
            }
        });

        // xparse's \NewDocumentCommand: the arguments described one letter
        // each -- m mandatory, o optional, O{default} optional with a default,
        // s a star and t<token> any other token that may follow the name, +
        // before a letter letting its argument hold a paragraph break.
        mouth.bind("\\declare", [this, &context, &mouth, &lexicon, named](Mouth&) {
            const bool global = std::exchange(context.global, false);
            const bool provided = std::exchange(context.provided, false);
            context.spanning = context.isolated = context.expanded = context.robust = false;

            const Token name = named("\\declare");
            if (name.empty()) return;

            const std::vector<Token> specification = mouth.argument({}, 0);
            const auto make = [&lexicon, &name](std::string_view text, Catcodes::Category category) {
                const Symbol symbol = lexicon.intern(text);
                return Token{symbol, category, name.location, lexicon.resolve(symbol)};
            };

            // What an o argument is when it was left out, spelled as xparse
            // spells it, so \IfNoValueTF can tell it from anything written.
            std::vector<Token> missing;
            for (const char letter : std::string_view("-NoValue-")) {
                missing.push_back(make(std::string_view(&letter, 1), letter == '-' ? Catcodes::Category::Other
                                                                                   : Catcodes::Category::Letter));
            }

            Mouth::Macro macro;
            for (std::size_t index = 0; index < specification.size(); ++index) {
                const Token& letter = specification[index];
                if (letter.category == Catcodes::Category::Space || letter.is('!')) continue;
                if (letter.is('+')) {
                    macro.spanning = true;
                    continue;
                }

                Mouth::Parameter parameter;
                if (letter.is('o')) {
                    parameter.optional = true;
                    parameter.fallbacks = missing;
                } else if (letter.is('O')) {
                    // The default is the brace group written after the O,
                    // or the one token if it is written bare.
                    parameter.optional = true;
                    std::size_t depth = 0;
                    while (++index < specification.size()) {
                        const Token& item = specification[index];
                        if (depth == 0 && item.category == Catcodes::Category::Space) continue;
                        if (item.is(Catcodes::Category::Group, '{') && depth++ == 0) continue;
                        if (item.is(Catcodes::Category::Group, '}') && --depth == 0) break;
                        parameter.fallbacks.push_back(item);
                        if (depth == 0) break;
                    }
                } else if (letter.is('s') || letter.is('t')) {
                    const Token mark = letter.is('s') ? make("*", Catcodes::Category::Other)
                                       : index + 1 < specification.size() ? specification[++index]
                                                                          : Token{};
                    if (mark.empty()) {
                        tracebacks.emplace_back(Traceback::Type::Argument, letter.location,
                                      std::format("\\declare{}: t needs the token it looks for", name.text));
                        return;
                    }
                    parameter.present = {mark, make("\\BooleanTrue", Catcodes::Category::Escape)};
                    parameter.fallbacks = {make("\\BooleanFalse", Catcodes::Category::Escape)};
                } else if (!letter.is('m')) {
                    tracebacks.emplace_back(Traceback::Type::Argument, letter.location,
                                  std::format("\\declare{}: an argument of kind {} is not supported",
                                              name.text, letter.text));
                    return;
                }

                if (macro.parameters.size() == static_cast<std::size_t>(parameters)) {
                    tracebacks.emplace_back(Traceback::Type::Argument, name.location,
                                  std::format("\\declare{} takes at most {} arguments", name.text, parameters));
                    return;
                }
                macro.parameters.push_back(std::move(parameter));
            }

            macro.body = mouth.argument({}, 1);
            if (provided && mouth.known(name.symbol)) return;
            mouth.define(name.symbol, std::move(macro), global);
        });

        // Plain TeX's \newif: `\switch\ifdraft` makes \ifdraft a conditional
        // that reads as \iffalse, and \drafttrue and \draftfalse the two
        // macros that turn it.
        mouth.bind("\\switch", [this, &context, &mouth, &lexicon](Mouth&) {
            const Token name = mouth.read();
            if (name.category != Catcodes::Category::Escape || !name.text.starts_with("\\if") ||
                name.text.size() == 3) {
                tracebacks.emplace_back(Traceback::Type::Macro, name.location,
                              "\\switch needs a name that begins with \\if");
                return;
            }

            const bool global = std::exchange(context.global, false);
            const std::string_view stem = name.text.substr(3);
            mouth.lend(name.symbol, falsehood);

            for (const bool value : {true, false}) {
                const Symbol symbol = lexicon.intern(std::format("\\{}{}", stem, value ? "true" : "false"));
                const Symbol meaning = value ? truth : falsehood;
                Mouth::Macro macro;
                macro.body = {Token{alias, Catcodes::Category::Escape, name.location, lexicon.resolve(alias)}, name,
                              Token{meaning, Catcodes::Category::Escape, name.location, lexicon.resolve(meaning)}};
                mouth.define(symbol, std::move(macro), global);
            }
        });

        mouth.bind("\\forget", [this, &context, &mouth](Mouth&) {
            const Token name = mouth.read();
            if (name.category != Catcodes::Category::Escape) {
                tracebacks.emplace_back(Traceback::Type::Macro, name.location,
                              "\\forget needs a control sequence");
                return;
            }
            if (mouth.macro(name.symbol) == nullptr) {
                tracebacks.emplace_back(Traceback::Type::Macro, name.location,
                              std::format("\\forget{} is not defined", name.text));
                return;
            }
            mouth.forget(name.symbol, std::exchange(context.global, false));
        });

        mouth.bind("\\alias", [this, &context, &mouth](Mouth&) {
            const Token name = mouth.read();

            // TeX's optional equals sign, and the one space it allows after:
            // `\let\a=\b` and `\let\a = \b` are `\let\a\b`.
            Token source = mouth.read();
            while (source.category == Catcodes::Category::Space) source = mouth.read();
            if (source.is(Catcodes::Category::Other, '=')) {
                source = mouth.read();
                if (source.category == Catcodes::Category::Space) source = mouth.read();
            }

            if ((name.category != Catcodes::Category::Escape && name.category != Catcodes::Category::Active) ||
                source.empty()) {
                tracebacks.emplace_back(Traceback::Type::Macro, name.location,
                              "\\alias needs a control sequence and what it is to mean");
                return;
            }
            const bool global = std::exchange(context.global, false);

            if (const Mouth::Macro* found = mouth.macro(source.symbol)) {
                mouth.define(name.symbol, *found, global);
                return;
            }

            // Whatever else the name comes to mean, a macro it had would go
            // on winning over it, so that goes first.
            if (mouth.macro(name.symbol)) mouth.forget(name.symbol, global);

            // Aliasing a primitive copies its handler, so the new name behaves
            // identically rather than merely expanding to the old one.
            if (mouth.handler(source.symbol)) {
                mouth.lend(name.symbol, source.symbol);
                return;
            }
            mouth.bind(name.symbol, {});
            if (!context.after.empty()) {
                const Token after = std::exchange(context.after, Token{});
                mouth.stream().inject(std::span{&after, 1});
            }

            // A command the parser builds, copied there; failing that, a
            // character, which the name then stands for; and failing both an
            // undefined name, which leaves this one undefined too, as TeX's
            // `\let\name\undefined` does.
            if (mouth.reader.lend && mouth.reader.lend(name.symbol, source.symbol)) return;
            if (source.category != Catcodes::Category::Escape) {
                Mouth::Macro macro;
                macro.body = {source};
                macro.implicit = true;
                mouth.define(name.symbol, std::move(macro), global);
            }
        });

        // Text as tokens of its own: each character an ordinary one, and a
        // blank a space -- what \string, \detokenize and \meaning print.
        const auto spelled = [&lexicon](const std::string_view written, const memory::Location location) {
            std::vector<Token> tokens;
            tokens.reserve(written.size());
            for (std::size_t at = 0; at < written.size();) {
                const auto lead = static_cast<unsigned char>(written[at]);
                const std::size_t span = lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
                const std::string_view piece = written.substr(at, span);
                at += span;
                const Symbol symbol = lexicon.intern(piece);
                tokens.push_back(Token{symbol, piece == " " ? Catcodes::Category::Space : Catcodes::Category::Other,
                                       location, lexicon.resolve(symbol)});
            }
            return tokens;
        };

        // How a token reads when written out again: a control word with the
        // space that ends it, anything else as it stands.
        const auto written = [](const std::vector<Token>& tokens) {
            std::string text;
            for (const Token& token : tokens) {
                text += token.text;
                if (token.category == Catcodes::Category::Escape && token.text.size() > 1 &&
                    ((token.text.back() >= 'a' && token.text.back() <= 'z') ||
                     (token.text.back() >= 'A' && token.text.back() <= 'Z') || token.text.back() == '@')) {
                    text += ' ';
                }
            }
            return text;
        };

        // \string: the next token's own name, as characters; \detokenize: a
        // group's, the same way.
        mouth.bind("\\string", [&mouth, spelled](Mouth&) {
            const Token token = mouth.read();
            if (token.empty()) return;
            const std::vector<Token> tokens = spelled(token.text, token.location);
            mouth.stream().inject(std::span{tokens});
        });
        mouth.bind("\\detokenize", [&mouth, spelled, written](Mouth&) {
            const memory::Location location = mouth.lookahead().location;
            const std::vector<Token> tokens = spelled(written(mouth.argument({}, 1)), location);
            if (!tokens.empty()) mouth.stream().inject(std::span{tokens});
        });

        // \meaning: what a name means, in TeX's words -- `macro:#1->...` for
        // a macro, its own name for a primitive, `undefined` for neither.
        mouth.bind("\\meaning", [&mouth, spelled, written](Mouth&) {
            const Token token = mouth.read();
            if (token.empty()) return;
            std::string meaning;
            if (const Mouth::Macro* found = mouth.macro(token.symbol)) {
                meaning = "macro:";
                for (std::size_t index = 0; index < found->parameters.size(); ++index) {
                    meaning += std::format("#{}", index + 1) + written(found->parameters[index].delimiters);
                }
                meaning += "->" + written(found->body);
            } else if (token.category == Catcodes::Category::Escape || token.category == Catcodes::Category::Active) {
                meaning = mouth.known(token.symbol) ? std::string(token.text) : "undefined";
            } else {
                meaning = (token.category == Catcodes::Category::Letter ? "the letter " : "the character ") +
                          std::string(token.text);
            }
            const std::vector<Token> tokens = spelled(meaning, token.location);
            mouth.stream().inject(std::span{tokens});
        });

        // \noexpand and e-TeX's \unexpanded: while an \edef reads, a name
        // kept from expanding, which it stores as written; anywhere else a
        // macro or a primitive so kept means nothing, as TeX's \relax, and
        // any other token is read as it is.
        mouth.bind("\\noexpand", [this, &mouth](Mouth&) {
            Token token = mouth.read();
            if (token.empty()) return;
            const bool name = token.category == Catcodes::Category::Escape || token.category == Catcodes::Category::Active;
            if (flattening > 0 && name) token.symbol = none;
            else if (name && (mouth.macro(token.symbol) || mouth.handler(token.symbol))) return;
            mouth.stream().inject(std::span{&token, 1});
        });
        mouth.bind("\\unexpanded", [this, &mouth](Mouth&) {
            // Its text is e-TeX's general text: what stands before the brace
            // is expanded until one does, so `\\unexpanded\\expandafter{\\x}`
            // holds what \\x holds rather than the \\expandafter.
            while (!mouth.lookahead().empty() && !mouth.lookahead().is(Catcodes::Category::Group, '{') &&
                   mouth.step()) {
            }
            std::vector<Token> tokens = mouth.argument({}, 1);
            if (flattening > 0) {
                for (Token& token : tokens) {
                    if (token.category == Catcodes::Category::Escape || token.category == Catcodes::Category::Active) {
                        token.symbol = none;
                    }
                }
            }
            if (!tokens.empty()) mouth.stream().inject(std::span{tokens});
        });

        // \uppercase and \lowercase: a group's letters in the other case,
        // everything else as it was; LaTeX's \MakeUppercase and
        // \MakeLowercase expand the group first, so a macro's own letters
        // change too.
        const auto cased = [&lexicon](std::vector<Token> tokens, const bool upper) {
            for (Token& token : tokens) {
                if (token.category == Catcodes::Category::Escape || token.text.size() != 1) continue;
                const char letter = token.text[0];
                const char turned = upper && letter >= 'a' && letter <= 'z'   ? static_cast<char>(letter - 32)
                                    : !upper && letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter + 32)
                                                                               : letter;
                if (turned == letter) continue;
                token.symbol = lexicon.intern(std::string_view(&turned, 1));
                token.text = lexicon.resolve(token.symbol);
            }
            return tokens;
        };
        for (const bool upper : {true, false}) {
            mouth.bind(upper ? "\\uppercase" : "\\lowercase", [&mouth, cased, upper](Mouth&) {
                const std::vector<Token> tokens = cased(mouth.argument({}, 1), upper);
                if (!tokens.empty()) mouth.stream().inject(std::span{tokens});
            });
            mouth.bind(upper ? "\\MakeUppercase" : "\\MakeLowercase", [&mouth, cased, flatten, upper](Mouth&) {
                const std::vector<Token> tokens = cased(flatten(mouth.argument({}, 1)), upper);
                if (!tokens.empty()) mouth.stream().inject(std::span{tokens});
            });
        }

        // \afterassignment: a token read once the next assignment is made;
        // \aftergroup: one read once the group it is written in closes.
        mouth.bind("\\afterassignment", [&context, &mouth](Mouth&) { context.after = mouth.read(); });
        mouth.bind("\\aftergroup", [&mouth](Mouth&) {
            if (const Token token = mouth.read(); !token.empty()) mouth.defer(token);
        });

        // \futurelet\name\first\second: \name made to mean \second, then
        // \first read with \second after it -- how a macro looks at what
        // follows it without taking it.
        mouth.bind("\\futurelet", [this, &mouth, &lexicon](Mouth&) {
            const Token name = mouth.read();
            const Token first = mouth.read();
            const Token second = mouth.read();
            const std::array<Token, 5> read{
                Token{alias, Catcodes::Category::Escape, name.location, lexicon.resolve(alias)}, name, second, first,
                second,
            };
            mouth.stream().inject(std::span{read});
        });

        // e-TeX's \scantokens: a group's tokens written out and read again,
        // under whatever the character categories are now.
        mouth.bind("\\scantokens", [&mouth, written](Mouth&) {
            const std::string text = written(mouth.argument({}, 1));
            if (!text.empty()) mouth.ingest(mouth.arena.copy(text));
        });

        // e-TeX's \protected, a prefix saying a macro is not to expand in an
        // \edef, which keeps it as written: LaTeX's robust commands.
        mouth.bind("\\protected", [&context](Mouth&) { context.robust = true; });

        // TeX's \relax: nothing at all, which is what a definition writes to
        // end a number or a length without leaving anything behind.
        mouth.bind("\\relax", [](Mouth&) {});

        // A name built from the text between \csname and \endcsname, expanded
        // as it is read. One that means nothing yet comes to mean \relax, as
        // it does in TeX, so writing it is harmless and defining it works.
        mouth.bind("\\csname", [this, &mouth, &lexicon](Mouth&) {
            const memory::Location location = mouth.lookahead().location;
            std::string written = "\\";
            for (Token token = mouth.expand(); !token.empty() && token.symbol != terminate; token = mouth.expand()) {
                written += token.text;
            }

            const Symbol symbol = lexicon.intern(written);
            if (!mouth.known(symbol)) mouth.lend(symbol, relax);
            const Token made{symbol, Catcodes::Category::Escape, location, lexicon.resolve(symbol)};
            mouth.stream().inject(std::span{&made, 1});
        });

        // Holds the next token back and expands the one after it once, then
        // puts the first back in front of whatever that became.
        mouth.bind("\\expandafter", [](Mouth& mouth) {
            const Token first = mouth.read();
            if (first.empty()) return;
            mouth.step();
            mouth.stream().inject(std::span{&first, 1});
        });

        // LaTeX's \@ifstar and \@ifnextchar: the tests a command makes of what
        // follows it. Each looks at the next token as written, past any
        // blanks, and puts back whatever it did not take -- \ifstar takes the
        // star it found, as LaTeX's does, so the branch it chose reads on
        // from after it. A branch may hold a paragraph, as a \long macro's
        // argument may: LaTeX's tests are \long.
        mouth.bind("\\ifstar", [](Mouth& mouth) {
            const std::vector<Token> starred = mouth.argument({}, 1);
            const std::vector<Token> plain = mouth.argument({}, 1);

            Token next = mouth.read();
            while (next.category == Catcodes::Category::Space) next = mouth.read();
            const bool star = next.is('*');
            if (!star && !next.empty()) mouth.stream().inject(std::span{&next, 1});

            const std::vector<Token>& chosen = star ? starred : plain;
            if (!chosen.empty()) mouth.stream().inject(std::span{chosen});
        });

        mouth.bind("\\ifnextchar", [](Mouth& mouth) {
            const std::vector<Token> wanted = mouth.argument({}, 1);
            const std::vector<Token> matching = mouth.argument({}, 1);
            const std::vector<Token> otherwise = mouth.argument({}, 1);

            Token next = mouth.read();
            while (next.category == Catcodes::Category::Space) next = mouth.read();
            // `\@ifnextchar\bgroup` asks after a brace group, which is the
            // one thing no argument can hold alone.
            const bool match = !wanted.empty() && !next.empty() &&
                               (next.text == wanted.front().text ||
                                (wanted.front().text == "\\bgroup" && next.is(Catcodes::Category::Group, '{')));
            if (!next.empty()) mouth.stream().inject(std::span{&next, 1});

            const std::vector<Token>& chosen = match ? matching : otherwise;
            if (!chosen.empty()) mouth.stream().inject(std::span{chosen});
        });

        Logger::log(Logger::Type::Semantics, Logger::Level::Debug, "Bound definition primitives");
    }

}
