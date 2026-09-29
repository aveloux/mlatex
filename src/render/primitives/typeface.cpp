/// @file
/// @brief Font selection primitives: `\\textfont`, `\\mathfont`, `\\font` and `\\@face`.
///
/// Nothing here opens a file. The registry is asked for a family at a size and
/// returns a font it may already have built; the bytes behind it come from the
/// library, which read them at most once for the whole run.
#include "render/primitives/typeface.hpp"
#include "syntax/argument.hpp"
#include "logger.hpp"

#include "syntax/number.hpp"

#include <array>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace render::primitives {

    Typeface::Typeface(syntax::Lexicon& lexicon) noexcept {
        lexicon.intern("\\font");
    }

    void Typeface::operator()(syntax::Parser& parser, Context& context) const {
        parser.mouth().bind("\\textfont", [this, &context](syntax::Mouth& mouth) {
            const std::string name = syntax::Argument::text(mouth);
            const typography::Font* font = context.registry.get({
                .family = name,
                .size = context.selection.text() ? context.selection.text()->size() : 12.0f
            });

            if (!font) {
                tracebacks_.emplace_back(syntax::Traceback::Type::Argument,
                                         mouth.lookahead().location,
                                         "No font family named '" + name + "'");
                return;
            }

            context.selection.text(font);
            Logger::log(Logger::Type::Layout, Logger::Level::Informative,
                        "Text face is now '{}'", name);
        });

        parser.mouth().bind("\\mathfont", [this, &context](syntax::Mouth& mouth) {
            const std::string name = syntax::Argument::text(mouth);
            const typography::Font* font = context.registry.get({
                .family = name,
                .size = context.selection.text() ? context.selection.text()->size() : 12.0f
            });

            if (!font) {
                tracebacks_.emplace_back(syntax::Traceback::Type::Argument,
                                         mouth.lookahead().location,
                                         "No font family named '" + name + "'");
                return;
            }

            context.selection.formula(font);
            Logger::log(Logger::Type::Layout, Logger::Level::Informative,
                        "Formula face is now '{}'", name);
        });

        // `\\font\name = family at size`: builds the font and binds the name to
        // selecting it, so the two steps a document usually wants together are
        // written once and used many times.
        parser.mouth().bind("\\font", [this, &context](syntax::Mouth& mouth) {
            const syntax::Token target = mouth.read();
            if (target.category != syntax::CatCodes::Category::Escape) {
                tracebacks_.emplace_back(syntax::Traceback::Type::Argument, target.location,
                                         "\\font needs a control sequence to name the face");
                return;
            }

            // Blanks around the `=` are the document being readable; neither
            // the sign nor the spaces mean anything.
            const auto blanks = [&mouth] {
                while (mouth.lookahead().category == syntax::CatCodes::Category::Space) {
                    mouth.read();
                }
            };

            blanks();
            syntax::Token equals = mouth.read();
            if (equals.text != "=") mouth.stream().inject(std::span{&equals, 1});
            blanks();

            // The family runs to the next blank, or is a brace group when one
            // is written -- which is how a family whose name has a space in it
            // is given.
            std::string name;
            if (mouth.lookahead().is(syntax::CatCodes::Category::Group, '{')) {
                name = syntax::Argument::text(mouth);
            } else {
                while (true) {
                    const syntax::Token next = mouth.lookahead();
                    if (next.empty() || next.category == syntax::CatCodes::Category::Space ||
                        next.category == syntax::CatCodes::Category::Escape) {
                        break;
                    }
                    name += mouth.read().text;
                }
            }

            float size = context.selection.text() ? context.selection.text()->size() : 12.0f;

            // The lexer emits one token per character, so the keyword `at` is
            // two of them and has to be matched as two. Neither is consumed
            // unless both are there, so a family followed by anything else
            // keeps its size and leaves that anything else alone.
            blanks();
            if (mouth.lookahead(0).is('a') && mouth.lookahead(1).is('t')) {
                mouth.read();
                mouth.read();
                if (const auto scanned = syntax::Number::dimension(mouth, context.registers)) {
                    size = static_cast<float>(*scanned) / static_cast<float>(syntax::Number::scale);
                }
            }

            const typography::Font* font = context.registry.get({.family = name, .size = size});
            if (!font) {
                tracebacks_.emplace_back(syntax::Traceback::Type::Argument, target.location,
                                         "No font family named '" + name + "'");
                return;
            }

            mouth.bind(target.symbol, [&context, font](syntax::Mouth&) {
                context.selection.text(font);
            });

            Logger::log(Logger::Type::Layout, Logger::Level::Informative,
                        "Bound {} to '{}' at {} points", target.text, name, size);
        });

        // `\\@face{main}{TeX Gyre Termes}`: a face named as the system names
        // it, which fontspec's \\setmainfont and \\setmathfont ask for, set
        // in the one the engine carries that stands nearest it -- STIX Two
        // for Times and its kind, New Computer Modern for Computer Modern,
        // Latin Modern for itself, the TeX Gyre maths for their text faces.
        // Each is found by what its name holds, lower-cased and without its
        // spaces, the longer names first; one the engine has nothing near
        // leaves the face as it was, and says so.
        parser.mouth().bind("\\@face", [this, &context](syntax::Mouth& mouth) {
            const memory::Location origin = mouth.lookahead().location;
            const std::string role = syntax::Argument::text(mouth);
            const std::string name = syntax::Argument::text(mouth);
            std::string key;
            for (const char letter : name) {
                if (letter == ' ' || letter == '-' || letter == '_') continue;
                key += static_cast<char>(letter >= 'A' && letter <= 'Z' ? letter + 32 : letter);
            }

            using Pair = std::pair<std::string_view, std::string_view>;
            static constexpr std::array<Pair, 20> texts{{
                {"newcomputermodernsans", "NewCMSans10-Regular"}, {"newcmsans", "NewCMSans10-Regular"},
                {"cmusans", "NewCMSans10-Regular"}, {"newcomputermodernmono", "NewCMMono10-Regular"},
                {"newcmmono", "NewCMMono10-Regular"}, {"cmutypewriter", "NewCMMono10-Regular"},
                {"newcomputermodern", "NewCM10-Regular"}, {"newcm", "NewCM10-Regular"},
                {"cmuserif", "NewCM10-Regular"}, {"computermodern", "NewCM10-Regular"},
                {"latinmodernsans", "lmsans10-regular"}, {"latinmodernmono", "lmmono10-regular"},
                {"latinmodern", "lmroman10-regular"}, {"stix", "STIXTwoText-Regular"},
                {"xits", "STIXTwoText-Regular"}, {"times", "STIXTwoText-Regular"},
                {"termes", "STIXTwoText-Regular"}, {"liberationserif", "STIXTwoText-Regular"},
                {"nimbusrom", "STIXTwoText-Regular"}, {"tinos", "STIXTwoText-Regular"},
            }};
            static constexpr std::array<Pair, 14> formulas{{
                {"newcomputermodernsansmath", "NewCMSansMath-Regular"}, {"newcmsansmath", "NewCMSansMath-Regular"},
                {"stix", "STIXTwoMath-Regular"}, {"xits", "STIXTwoMath-Regular"},
                {"cambria", "STIXTwoMath-Regular"}, {"fira", "FiraMath-Regular"}, {"asana", "Asana-Math"},
                {"pagella", "texgyrepagella-math"}, {"termes", "texgyretermes-math"},
                {"bonum", "texgyrebonum-math"}, {"schola", "texgyreschola-math"}, {"dejavu", "texgyredejavu-math"},
                {"newcomputermodern", "NewCMMath-Regular"}, {"latinmodern", "NewCMMath-Regular"},
            }};

            const bool formula = role == "math";
            std::string_view file;
            for (const auto& [part, carried] : formula ? std::span<const Pair>{formulas} : std::span<const Pair>{texts}) {
                if (key.contains(part)) {
                    file = carried;
                    break;
                }
            }

            const typography::Font* current = formula ? context.selection.formula() : context.selection.text();
            const float size = current ? current->size() : 10.0f;
            const typography::Font* font = file.empty() ? nullptr : context.registry.get({.family = file, .size = size});
            if (!font) {
                tracebacks_.emplace_back(syntax::Traceback::Type::Warning, origin,
                                         std::format("fontspec: no face near '{}' is carried; the {} face is kept", name,
                                                     formula ? "formulas'" : "text's"));
                return;
            }

            // A main face is the text's from here on and the page's number's;
            // a sans or a mono one only when it is the face the text is in.
            if (formula) {
                context.selection.formula(font);
                context.document.fallback(font);
            } else if (role == "main") {
                context.selection.text(font);
                context.document.furniture().face = font;
            }
            Logger::log(Logger::Type::Layout, Logger::Level::Informative, "The {} face '{}' is set in '{}'", role, name,
                        file);
        });

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound typeface primitives");
    }

}
