/// @file
/// @brief Language primitives: a language's patterns, its direction and its
///        captions, chosen by name.
///
/// Everything here is either a lookup in the table of languages below or an
/// instruction to the document. The captions themselves are words, and live
/// in babel's module, a file per language; this only asks for them.
#include "render/primitives/languages.hpp"
#include "logger.hpp"

#include "syntax/argument.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace render::primitives {

    /// Every language known, by each name babel or polyglossia gives it: the
    /// file of its captions, its patterns, how near a word's ends it breaks
    /// -- hyph-utf8's own figures for each -- and whether it reads right to
    /// left. A language with no patterns of its own breaks no word; one with
    /// no captions of its own keeps whatever captions were in force.
    static constexpr std::array<Languages::Language, 78> languages{{
        {"english", "english", "en-us", 2, 3, false},
        {"american", "english", "en-us", 2, 3, false},
        {"USenglish", "english", "en-us", 2, 3, false},
        {"canadian", "english", "en-us", 2, 3, false},
        {"british", "english", "en-gb", 2, 3, false},
        {"UKenglish", "english", "en-gb", 2, 3, false},
        {"australian", "english", "en-gb", 2, 3, false},
        {"newzealand", "english", "en-gb", 2, 3, false},
        {"german", "german", "de-1901", 2, 2, false},
        {"ngerman", "german", "de-1996", 2, 2, false},
        {"austrian", "german", "de-1901", 2, 2, false},
        {"naustrian", "german", "de-1996", 2, 2, false},
        {"swissgerman", "german", "de-ch-1901", 2, 2, false},
        {"nswissgerman", "german", "de-ch-1901", 2, 2, false},
        {"french", "french", "fr", 2, 2, false},
        {"francais", "french", "fr", 2, 2, false},
        {"acadian", "french", "fr", 2, 2, false},
        {"canadien", "french", "fr", 2, 2, false},
        {"spanish", "spanish", "es", 2, 2, false},
        {"mexican", "spanish", "es", 2, 2, false},
        {"italian", "italian", "it", 2, 2, false},
        {"portuguese", "portuguese", "pt", 2, 3, false},
        {"portuges", "portuguese", "pt", 2, 3, false},
        {"brazil", "brazilian", "pt", 2, 3, false},
        {"brazilian", "brazilian", "pt", 2, 3, false},
        {"dutch", "dutch", "nl", 2, 2, false},
        {"afrikaans", "", "af", 1, 2, false},
        {"russian", "russian", "ru", 2, 2, false},
        {"ukrainian", "ukrainian", "uk", 2, 2, false},
        {"belarusian", "", "be", 2, 2, false},
        {"bulgarian", "bulgarian", "bg", 2, 2, false},
        {"macedonian", "", "mk", 2, 2, false},
        {"serbian", "", "sh-latn", 2, 2, false},
        {"serbianc", "", "sr-cyrl", 2, 2, false},
        {"polish", "polish", "pl", 2, 2, false},
        {"czech", "czech", "cs", 2, 3, false},
        {"slovak", "", "sk", 2, 3, false},
        {"slovene", "", "sl", 2, 2, false},
        {"slovenian", "", "sl", 2, 2, false},
        {"croatian", "croatian", "hr", 2, 2, false},
        {"hungarian", "hungarian", "hu", 2, 2, false},
        {"magyar", "hungarian", "hu", 2, 2, false},
        {"romanian", "romanian", "ro", 2, 2, false},
        {"greek", "greek", "el-monoton", 1, 1, false},
        {"polutonikogreek", "greek", "el-polyton", 1, 1, false},
        {"ancientgreek", "", "grc", 1, 1, false},
        {"turkish", "turkish", "tr", 2, 2, false},
        {"swedish", "swedish", "sv", 2, 2, false},
        {"danish", "danish", "da", 2, 2, false},
        {"norsk", "norsk", "nb", 2, 2, false},
        {"bokmal", "norsk", "nb", 2, 2, false},
        {"norwegian", "norsk", "nb", 2, 2, false},
        {"nynorsk", "norsk", "nn", 2, 2, false},
        {"finnish", "finnish", "fi", 2, 2, false},
        {"estonian", "", "et", 2, 3, false},
        {"icelandic", "", "is", 2, 2, false},
        {"latvian", "", "lv", 2, 2, false},
        {"lithuanian", "", "lt", 2, 2, false},
        {"catalan", "catalan", "ca", 2, 2, false},
        {"basque", "", "eu", 2, 2, false},
        {"galician", "", "gl", 2, 2, false},
        {"irish", "", "ga", 2, 3, false},
        {"welsh", "", "cy", 2, 3, false},
        {"latin", "", "la", 2, 2, false},
        {"esperanto", "", "eo", 2, 2, false},
        {"indonesian", "", "id", 2, 2, false},
        {"bahasa", "", "id", 2, 2, false},
        {"armenian", "", "hy", 1, 2, false},
        {"georgian", "", "ka", 1, 2, false},
        {"interlingua", "", "ia", 2, 2, false},
        {"albanian", "", "sq", 2, 2, false},
        {"thai", "", "th", 2, 3, false},
        {"arabic", "arabic", "", 0, 0, true},
        {"hebrew", "hebrew", "", 0, 0, true},
        {"persian", "persian", "", 0, 0, true},
        {"farsi", "persian", "", 0, 0, true},
        {"urdu", "", "", 0, 0, true},
        {"syriac", "", "", 0, 0, true},
    }};

    Languages::Languages(syntax::Lexicon& lexicon) noexcept {
        lexicon.intern("\\@language");
        lexicon.intern("\\@hyphenation");
        lexicon.intern("\\@direction");
        lexicon.intern("\\@languagecommands");
    }

    const Languages::Language* Languages::get(const std::string_view name) noexcept {
        const auto found = std::ranges::find(languages, name, &Language::name);
        return found == languages.end() ? nullptr : &*found;
    }

    void Languages::operator()(syntax::Parser& parser, Context& context) const {
        using Command = layout::Node::Directive::Command;

        // A language's patterns, read the first time it is chosen and kept:
        // English is compiled into the engine, the rest read from the
        // assets' folder of them.
        const auto patterns = [this, &context](const Language& language) -> const typography::Hyphenator* {
            if (language.patterns.empty()) return nullptr;
            for (std::size_t index = 0; index < read.size(); ++index) {
                if (read[index] == language.patterns) return &hyphenators[index];
            }
            typography::Hyphenator& made = hyphenators.emplace_back();
            read.push_back(language.patterns);
            const std::string file = std::format("hyph-{}.pat.txt", language.patterns);
            if (made.embed(file) == 0 && !context.patterns.empty()) made.compose(context.patterns + "/" + file);
            return &made;
        };

        // One instruction to the document, in its place among the material.
        // A language's words are told to the document at once as well:
        // what a box or a cell holds is set as it is read, long before the
        // instruction is reached in its place, and is set in the language
        // chosen by then.
        const auto order = [&context](syntax::Parser& parser, const layout::Node::Directive& said) -> syntax::Node* {
            if (said.command == Command::Language) context.document.hyphenate(said.hyphenator, said.before, said.after);
            memory::Arena& arena = parser.arena;
            auto* mark = arena.compose<layout::Node>(layout::Node::Type::Directive);
            mark->directive(said);
            return directive(arena, mark, parser.mouth.lookahead().location);
        };

        // The language a list of names means: babel's options in the order
        // they were written, the last the language the document is in unless
        // `main=` names another. What is not a language -- babel's other
        // options, `shorthands=off` -- is passed over.
        const auto named = [this](syntax::Mouth& mouth) -> const Language* {
            const memory::Location origin = mouth.lookahead().location;
            const std::string list = syntax::Argument::expanded(mouth);

            const Language* found = nullptr;
            const Language* chosen = nullptr;
            for (std::size_t start = 0; start <= list.size();) {
                std::size_t stop = list.find(',', start);
                if (stop == std::string::npos) stop = list.size();
                std::string_view item(list.data() + start, stop - start);
                start = stop + 1;
                while (!item.empty() && (item.front() == ' ' || item.front() == '\n')) item.remove_prefix(1);
                while (!item.empty() && (item.back() == ' ' || item.back() == '\n')) item.remove_suffix(1);

                if (item.starts_with("main=")) {
                    chosen = get(item.substr(5));
                } else if (const Language* language = get(item)) {
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
        // rest comes to is read after it.
        parser.bind("\\@language", [this, patterns, order, named](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            const Language* language = named(mouth);
            if (!language) return nullptr;
            current = language->name;

            std::string said = std::format("\\@direction{{{}}}\\@define\\languagename{{{}}}",
                                           language->reversed ? "RTL" : "LTR", language->name);
            if (!language->captions.empty()) {
                if (!std::ranges::contains(captioned, language->captions)) {
                    captioned.push_back(language->captions);
                    said += std::format("\\input{{babel/{}}}", language->captions);
                }
                said += std::format("\\csname captions{0}\\endcsname\\csname date{0}\\endcsname", language->captions);
            }
            mouth.ingest(mouth.arena.copy(said));
            return order(parser, {.command = Command::Language, .hyphenator = patterns(*language),
                                  .before = language->left, .after = language->right});
        });

        // Its words alone: a phrase in it inside the text of another.
        parser.bind("\\@hyphenation", [patterns, order, named](syntax::Parser& parser) -> syntax::Node* {
            const Language* language = named(parser.mouth);
            if (!language) return nullptr;
            return order(parser, {.command = Command::Language, .hyphenator = patterns(*language),
                                  .before = language->left, .after = language->right});
        });

        // Which way the paragraphs from here on read: `RTL`, right to left,
        // or anything else, left to right.
        parser.bind("\\@direction", [order, &context](syntax::Parser& parser) -> syntax::Node* {
            const std::string way = syntax::Argument::expanded(parser.mouth);
            context.reversed = way == "RTL";
            return order(parser, {.command = Command::Direction, .reversed = context.reversed});
        });

        // A block in another language: its paragraphs apart from those
        // around it and set in that language -- wholly, captions too, or
        // with a star its words and direction alone -- and the language
        // before it chosen again as it closes.
        const auto block = [this, &context](const std::string_view name, const bool whole, const std::string_view fixed) {
            context.blocks.watch(
                name,
                [this, whole, fixed = std::string(fixed)](syntax::Mouth& mouth) {
                    const std::string language = fixed.empty() ? syntax::Argument::expanded(mouth) : fixed;
                    kept.push_back(current);
                    const Language* known = get(language);
                    mouth.ingest(mouth.arena.copy(
                        whole ? std::format("\\par\\@language{{{}}}", language)
                              : std::format("\\par\\@hyphenation{{{}}}\\@direction{{{}}}", language,
                                            known && known->reversed ? "RTL" : "LTR")));
                },
                [this, whole](syntax::Mouth& mouth) {
                    const std::string before = kept.empty() ? current : kept.back();
                    if (!kept.empty()) kept.pop_back();
                    const Language* known = get(before);
                    mouth.ingest(mouth.arena.copy(
                        whole ? std::format("\\par\\@language{{{}}}", before)
                              : std::format("\\par\\@hyphenation{{{}}}\\@direction{{{}}}", before,
                                            known && known->reversed ? "RTL" : "LTR")));
                });
        };
        block("otherlanguage", true, "");
        block("otherlanguage*", false, "");
        block("hyphenrules", false, "");

        // polyglossia's commands for a language it is told the document
        // quotes: \textrussian{...}, and a block named for it -- `Arabic`
        // for Arabic, since \arabic is a counter's numbering already.
        parser.mouth.bind("\\@languagecommands", [this, block](syntax::Mouth& mouth) {
            const std::string list = syntax::Argument::expanded(mouth);
            std::string said;
            for (std::size_t start = 0; start <= list.size();) {
                std::size_t stop = list.find(',', start);
                if (stop == std::string::npos) stop = list.size();
                std::string_view item(list.data() + start, stop - start);
                start = stop + 1;
                while (!item.empty() && item.front() == ' ') item.remove_prefix(1);
                while (!item.empty() && item.back() == ' ') item.remove_suffix(1);

                const Language* language = get(item);
                if (!language) continue;
                said += std::format("\\@define\\text{0}{{\\foreignlanguage{{{0}}}}}", language->name);
                if (std::ranges::contains(watched, language->name)) continue;
                watched.push_back(language->name);
                block(language->name == "arabic" ? "Arabic" : language->name, true, language->name);
            }
            if (!said.empty()) mouth.ingest(mouth.arena.copy(said));
        });

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound the language primitives");
    }

}
