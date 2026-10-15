/// @file
/// @brief Language primitives: a language's patterns, its direction and its
///        captions, chosen by name.
///
/// Everything here is either a lookup in the table of languages below or an
/// instruction to the document. The captions themselves are words, and live
/// in babel's module, a file per language; this only asks for them.
#include "render/primitives/text/languages.hpp"
#include "logger.hpp"

#include "syntax/argument.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace render::primitives::text {
    /// Every language known, by each name babel or polyglossia gives it: the
    /// file of its captions, its patterns, how near a word's ends it breaks
    /// -- hyph-utf8's own figures for each -- and whether it reads right to
    /// left. A language with no patterns of its own breaks no word; one with
    /// no captions of its own keeps whatever captions were in force.
    static constexpr std::array<Languages::Language, 78> languages{
        {
            {.name = "english", .captions = "english", .patterns = "en-us", .left = 2, .right = 3, .reversed = false},
            {.name = "american", .captions = "english", .patterns = "en-us", .left = 2, .right = 3, .reversed = false},
            {.name = "USenglish", .captions = "english", .patterns = "en-us", .left = 2, .right = 3, .reversed = false},
            {.name = "canadian", .captions = "english", .patterns = "en-us", .left = 2, .right = 3, .reversed = false},
            {.name = "british", .captions = "english", .patterns = "en-gb", .left = 2, .right = 3, .reversed = false},
            {.name = "UKenglish", .captions = "english", .patterns = "en-gb", .left = 2, .right = 3, .reversed = false},
            {
                .name = "australian", .captions = "english", .patterns = "en-gb", .left = 2, .right = 3,
                .reversed = false
            },
            {
                .name = "newzealand", .captions = "english", .patterns = "en-gb", .left = 2, .right = 3,
                .reversed = false
            },
            {.name = "german", .captions = "german", .patterns = "de-1901", .left = 2, .right = 2, .reversed = false},
            {.name = "ngerman", .captions = "german", .patterns = "de-1996", .left = 2, .right = 2, .reversed = false},
            {.name = "austrian", .captions = "german", .patterns = "de-1901", .left = 2, .right = 2, .reversed = false},
            {
                .name = "naustrian", .captions = "german", .patterns = "de-1996", .left = 2, .right = 2,
                .reversed = false
            },
            {
                .name = "swissgerman", .captions = "german", .patterns = "de-ch-1901", .left = 2, .right = 2,
                .reversed = false
            },
            {
                .name = "nswissgerman", .captions = "german", .patterns = "de-ch-1901", .left = 2, .right = 2,
                .reversed = false
            },
            {.name = "french", .captions = "french", .patterns = "fr", .left = 2, .right = 2, .reversed = false},
            {.name = "francais", .captions = "french", .patterns = "fr", .left = 2, .right = 2, .reversed = false},
            {.name = "acadian", .captions = "french", .patterns = "fr", .left = 2, .right = 2, .reversed = false},
            {.name = "canadien", .captions = "french", .patterns = "fr", .left = 2, .right = 2, .reversed = false},
            {.name = "spanish", .captions = "spanish", .patterns = "es", .left = 2, .right = 2, .reversed = false},
            {.name = "mexican", .captions = "spanish", .patterns = "es", .left = 2, .right = 2, .reversed = false},
            {.name = "italian", .captions = "italian", .patterns = "it", .left = 2, .right = 2, .reversed = false},
            {
                .name = "portuguese", .captions = "portuguese", .patterns = "pt", .left = 2, .right = 3,
                .reversed = false
            },
            {.name = "portuges", .captions = "portuguese", .patterns = "pt", .left = 2, .right = 3, .reversed = false},
            {.name = "brazil", .captions = "brazilian", .patterns = "pt", .left = 2, .right = 3, .reversed = false},
            {.name = "brazilian", .captions = "brazilian", .patterns = "pt", .left = 2, .right = 3, .reversed = false},
            {.name = "dutch", .captions = "dutch", .patterns = "nl", .left = 2, .right = 2, .reversed = false},
            {.name = "afrikaans", .captions = "", .patterns = "af", .left = 1, .right = 2, .reversed = false},
            {.name = "russian", .captions = "russian", .patterns = "ru", .left = 2, .right = 2, .reversed = false},
            {.name = "ukrainian", .captions = "ukrainian", .patterns = "uk", .left = 2, .right = 2, .reversed = false},
            {.name = "belarusian", .captions = "", .patterns = "be", .left = 2, .right = 2, .reversed = false},
            {.name = "bulgarian", .captions = "bulgarian", .patterns = "bg", .left = 2, .right = 2, .reversed = false},
            {.name = "macedonian", .captions = "", .patterns = "mk", .left = 2, .right = 2, .reversed = false},
            {.name = "serbian", .captions = "", .patterns = "sh-latn", .left = 2, .right = 2, .reversed = false},
            {.name = "serbianc", .captions = "", .patterns = "sr-cyrl", .left = 2, .right = 2, .reversed = false},
            {.name = "polish", .captions = "polish", .patterns = "pl", .left = 2, .right = 2, .reversed = false},
            {.name = "czech", .captions = "czech", .patterns = "cs", .left = 2, .right = 3, .reversed = false},
            {.name = "slovak", .captions = "", .patterns = "sk", .left = 2, .right = 3, .reversed = false},
            {.name = "slovene", .captions = "", .patterns = "sl", .left = 2, .right = 2, .reversed = false},
            {.name = "slovenian", .captions = "", .patterns = "sl", .left = 2, .right = 2, .reversed = false},
            {.name = "croatian", .captions = "croatian", .patterns = "hr", .left = 2, .right = 2, .reversed = false},
            {.name = "hungarian", .captions = "hungarian", .patterns = "hu", .left = 2, .right = 2, .reversed = false},
            {.name = "magyar", .captions = "hungarian", .patterns = "hu", .left = 2, .right = 2, .reversed = false},
            {.name = "romanian", .captions = "romanian", .patterns = "ro", .left = 2, .right = 2, .reversed = false},
            {.name = "greek", .captions = "greek", .patterns = "el-monoton", .left = 1, .right = 1, .reversed = false},
            {
                .name = "polutonikogreek", .captions = "greek", .patterns = "el-polyton", .left = 1, .right = 1,
                .reversed = false
            },
            {.name = "ancientgreek", .captions = "", .patterns = "grc", .left = 1, .right = 1, .reversed = false},
            {.name = "turkish", .captions = "turkish", .patterns = "tr", .left = 2, .right = 2, .reversed = false},
            {.name = "swedish", .captions = "swedish", .patterns = "sv", .left = 2, .right = 2, .reversed = false},
            {.name = "danish", .captions = "danish", .patterns = "da", .left = 2, .right = 2, .reversed = false},
            {.name = "norsk", .captions = "norsk", .patterns = "nb", .left = 2, .right = 2, .reversed = false},
            {.name = "bokmal", .captions = "norsk", .patterns = "nb", .left = 2, .right = 2, .reversed = false},
            {.name = "norwegian", .captions = "norsk", .patterns = "nb", .left = 2, .right = 2, .reversed = false},
            {.name = "nynorsk", .captions = "norsk", .patterns = "nn", .left = 2, .right = 2, .reversed = false},
            {.name = "finnish", .captions = "finnish", .patterns = "fi", .left = 2, .right = 2, .reversed = false},
            {.name = "estonian", .captions = "", .patterns = "et", .left = 2, .right = 3, .reversed = false},
            {.name = "icelandic", .captions = "", .patterns = "is", .left = 2, .right = 2, .reversed = false},
            {.name = "latvian", .captions = "", .patterns = "lv", .left = 2, .right = 2, .reversed = false},
            {.name = "lithuanian", .captions = "", .patterns = "lt", .left = 2, .right = 2, .reversed = false},
            {.name = "catalan", .captions = "catalan", .patterns = "ca", .left = 2, .right = 2, .reversed = false},
            {.name = "basque", .captions = "", .patterns = "eu", .left = 2, .right = 2, .reversed = false},
            {.name = "galician", .captions = "", .patterns = "gl", .left = 2, .right = 2, .reversed = false},
            {.name = "irish", .captions = "", .patterns = "ga", .left = 2, .right = 3, .reversed = false},
            {.name = "welsh", .captions = "", .patterns = "cy", .left = 2, .right = 3, .reversed = false},
            {.name = "latin", .captions = "", .patterns = "la", .left = 2, .right = 2, .reversed = false},
            {.name = "esperanto", .captions = "", .patterns = "eo", .left = 2, .right = 2, .reversed = false},
            {.name = "indonesian", .captions = "", .patterns = "id", .left = 2, .right = 2, .reversed = false},
            {.name = "bahasa", .captions = "", .patterns = "id", .left = 2, .right = 2, .reversed = false},
            {.name = "armenian", .captions = "", .patterns = "hy", .left = 1, .right = 2, .reversed = false},
            {.name = "georgian", .captions = "", .patterns = "ka", .left = 1, .right = 2, .reversed = false},
            {.name = "interlingua", .captions = "", .patterns = "ia", .left = 2, .right = 2, .reversed = false},
            {.name = "albanian", .captions = "", .patterns = "sq", .left = 2, .right = 2, .reversed = false},
            {.name = "thai", .captions = "", .patterns = "th", .left = 2, .right = 3, .reversed = false},
            {.name = "arabic", .captions = "arabic", .patterns = "", .left = 0, .right = 0, .reversed = true},
            {.name = "hebrew", .captions = "hebrew", .patterns = "", .left = 0, .right = 0, .reversed = true},
            {.name = "persian", .captions = "persian", .patterns = "", .left = 0, .right = 0, .reversed = true},
            {.name = "farsi", .captions = "persian", .patterns = "", .left = 0, .right = 0, .reversed = true},
            {.name = "urdu", .captions = "", .patterns = "", .left = 0, .right = 0, .reversed = true},
            {.name = "syriac", .captions = "", .patterns = "", .left = 0, .right = 0, .reversed = true},
        }
    };

    Languages::Languages(syntax::Lexicon &lexicon) noexcept {
        lexicon.intern("\\@language");
        lexicon.intern("\\@hyphenation");
        lexicon.intern("\\@direction");
        lexicon.intern("\\@languagecommands");
    }

    const Languages::Language *Languages::get(const std::string_view name) noexcept {
        const auto found = std::ranges::find(languages, name, &Language::name);
        return found == languages.end() ? nullptr : &*found;
    }

    void Languages::operator()(syntax::Parser &parser, Context &context) const {
        using Command = layout::Node::Directive::Command;

        // A language's patterns, read the first time it is chosen and kept:
        // English is compiled into the engine, the rest read from the
        // assets' folder of them.
        const auto patterns = [this, &context](const Language &language) -> const typography::Hyphenator * {
            if (language.patterns.empty()) return nullptr;
            for (std::size_t index = 0; index < read.size(); ++index) {
                if (read[index] == language.patterns) return &hyphenators[index];
            }
            typography::Hyphenator &made = hyphenators.emplace_back();
            read.push_back(language.patterns);
            const std::string file = std::format("hyph-{}.pat.txt", language.patterns);
            if (made.embed(file) == 0 && !context.patterns.empty()) {
                made.compose(std::format("{}/{}", context.patterns, file));
            }
            return &made;
        };

        // One instruction to the document, in its place among the material.
        // A language's words are told to the document at once as well:
        // what a box or a cell holds is set as it is read, long before the
        // instruction is reached in its place, and is set in the language
        // chosen by then.
        const auto order = [&context](const syntax::Parser &parser,
                                      const layout::Node::Directive &said) -> syntax::Node * {
            if (said.command == Command::Language) context.document.hyphenate(said);
            memory::Arena &arena = parser.arena;
            auto *mark = arena.compose<layout::Node>(layout::Node::Type::Directive);
            mark->directive(said);
            return directive(arena, mark, parser.mouth.lookahead().location);
        };

        // A language's words as they are set: its patterns and how near a
        // word's ends they break; the digits it is written with -- Arabic's
        // own, ٠١٢٣, and Persian's and Urdu's, ۰۱۲۳, unless polyglossia was
        // told `numerals=maghrib` -- and French's spaces before its high
        // punctuation, as babel and polyglossia set them.
        const auto spoken = [this, patterns](const Language &language) -> layout::Node::Directive {
            const auto chosen = std::ranges::find(numerals, language.name, &std::pair<std::string, std::string>::first);
            const bool western = chosen != numerals.end() && (
                                     chosen->second == "maghrib" || chosen->second == "western");
            std::uint32_t digits = 0;
            if (language.name == "arabic") digits = 0x0660;
            if (language.name == "persian" || language.name == "farsi" || language.name == "urdu") digits = 0x06F0;
            return {
                .command = Command::Language, .hyphenator = patterns(language), .before = language.left,
                .after = language.right, .digits = western ? 0 : digits, .spaced = language.captions == "french"
            };
        };

        // polyglossia's options for a language, `[numerals=maghrib]`, read
        // ahead of its name and kept for it by name: its block chosen later
        // is written so too.
        const auto options = [](syntax::Mouth &mouth) {
            std::string written;
            for (const syntax::Token &token: mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                written += token.text;
            }
            return written;
        };
        const auto remember = [this](const std::string &written, const std::string_view name) {
            const std::size_t at = written.find("numerals=");
            if (at == std::string::npos) return;
            std::string value = written.substr(at + 9);
            value = value.substr(0, value.find(','));
            std::erase(value, ' ');
            if (const auto known = std::ranges::find(numerals, name, &std::pair<std::string, std::string>::first);
                known != numerals.end()) known->second = value;
            else numerals.emplace_back(std::string(name), value);
        };

        // The language a list of names means: babel's options in the order
        // they were written, the last the language the document is in unless
        // `main=` names another. What is not a language -- babel's other
        // options, `shorthands=off` -- is passed over.
        const auto named = [this](syntax::Mouth &mouth) -> const Language * {
            const memory::Location origin = mouth.lookahead().location;
            const std::string list = syntax::Argument::expanded(mouth);

            const Language *found = nullptr;
            const Language *chosen = nullptr;
            for (std::size_t start = 0; start <= list.size();) {
                std::size_t stop = list.find(',', start);
                if (stop == std::string::npos) stop = list.size();
                std::string_view item(list.data() + start, stop - start);
                start = stop + 1;
                while (!item.empty() && (item.front() == ' ' || item.front() == '\n')) item.remove_prefix(1);
                while (!item.empty() && (item.back() == ' ' || item.back() == '\n')) item.remove_suffix(1);

                if (item.starts_with("main=")) {
                    chosen = get(item.substr(5));
                } else if (const Language *language = get(item)) {
                    found = language;
                }
            }
            if (chosen) return chosen;
            if (!found && !list.empty()) {
                tracebacks.emplace_back(syntax::Traceback::Type::Warning, origin,
                                        std::format("No language known as '{}'; its words break as before", list));
            }
            return found;
        };

        // A language chosen whole: its words, its direction, the name
        // \languagename says, and its captions and date, its file of them
        // read the first time. The instruction goes first, then what the
        // rest comes to is read after it. polyglossia's options for it may
        // come first: `\@language[numerals=maghrib]{arabic}`.
        parser.bind("\\@language",
                    [this, order, named, spoken, options, remember](const syntax::Parser &parser) -> syntax::Node * {
                        syntax::Mouth &mouth = parser.mouth;
                        const std::string given = options(mouth);
                        const Language *language = named(mouth);
                        if (!language) return nullptr;
                        remember(given, language->name);
                        current = language->name;

                        std::string said = std::format(R"(\@direction{{{}}}\@define\languagename{{{}}})",
                                                       language->reversed ? "RTL" : "LTR", language->name);
                        if (!language->captions.empty()) {
                            if (!std::ranges::contains(captioned, language->captions)) {
                                captioned.push_back(language->captions);
                                said += std::format("\\input{{babel/{}}}", language->captions);
                            }
                            said += std::format(R"(\csname captions{0}\endcsname\csname date{0}\endcsname)",
                                                language->captions);
                        }
                        mouth.ingest(mouth.arena.copy(said));
                        return order(parser, spoken(*language));
                    });

        // Its words alone: a phrase in it inside the text of another, set as
        // it sets them -- its breaks, its digits, French's spaces.
        parser.bind("\\@hyphenation", [order, named, spoken](const syntax::Parser &parser) -> syntax::Node * {
            const Language *language = named(parser.mouth);
            if (!language) return nullptr;
            return order(parser, spoken(*language));
        });

        // Which way the paragraphs from here on read: `RTL`, right to left,
        // or anything else, left to right.
        parser.bind("\\@direction", [order, &context](const syntax::Parser &parser) -> syntax::Node * {
            const std::string way = syntax::Argument::expanded(parser.mouth);
            context.reversed = way == "RTL";
            return order(parser, {.command = Command::Direction, .reversed = context.reversed});
        });

        // A block in another language: its paragraphs apart from those
        // around it and set in that language -- wholly, captions too, or
        // with a star its words and direction alone -- and the language
        // before it chosen again as it closes.
        const auto block = [this, &context
                ](const std::string_view name, const bool whole, const std::string_view fixed) {
            context.environments.watch(
                name,
                [this, whole, given = std::string(fixed)](syntax::Mouth &mouth) {
                    const std::string language = given.empty() ? syntax::Argument::expanded(mouth) : given;
                    kept.push_back(current);
                    const Language *known = get(language);
                    mouth.ingest(mouth.arena.copy(
                        whole
                            ? std::format("\\par\\@language{{{}}}", language)
                            : std::format(R"(\par\@hyphenation{{{}}}\@direction{{{}}})", language,
                                          known && known->reversed ? "RTL" : "LTR")));
                },
                [this, whole](syntax::Mouth &mouth) {
                    const std::string before = kept.empty() ? current : kept.back();
                    if (!kept.empty()) kept.pop_back();
                    const Language *known = get(before);
                    mouth.ingest(mouth.arena.copy(
                        whole
                            ? std::format("\\par\\@language{{{}}}", before)
                            : std::format(R"(\par\@hyphenation{{{}}}\@direction{{{}}})", before,
                                          known && known->reversed ? "RTL" : "LTR")));
                });
        };
        block("otherlanguage", true, "");
        block("otherlanguage*", false, "");
        block("hyphenrules", false, "");

        // polyglossia's commands for a language it is told the document
        // quotes: \textrussian{...}, and a block named for it -- `Arabic`
        // for Arabic, since \arabic is a counter's numbering already. Its
        // options, `\@languagecommands[numerals=maghrib]{arabic}`, are kept
        // for each language named.
        parser.mouth.bind("\\@languagecommands", [this, block, options, remember](syntax::Mouth &mouth) {
            const std::string given = options(mouth);
            const std::string list = syntax::Argument::expanded(mouth);
            std::string said;
            for (std::size_t start = 0; start <= list.size();) {
                std::size_t stop = list.find(',', start);
                if (stop == std::string::npos) stop = list.size();
                std::string_view item(list.data() + start, stop - start);
                start = stop + 1;
                while (!item.empty() && item.front() == ' ') item.remove_prefix(1);
                while (!item.empty() && item.back() == ' ') item.remove_suffix(1);

                const Language *language = get(item);
                if (!language) continue;
                remember(given, language->name);
                said += std::format(R"(\@define\text{0}{{\foreignlanguage{{{0}}}}})", language->name);
                if (std::ranges::contains(watched, language->name)) continue;
                watched.push_back(language->name);
                block(language->name == "arabic" ? "Arabic" : language->name, true, language->name);
            }
            if (!said.empty()) mouth.ingest(mouth.arena.copy(said));
        });

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound the language primitives");
    }
}
