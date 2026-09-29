/// @file
/// @brief Style primitives: family, series, shape and size.
///
/// Both forms of every style share one closure. A `\\textbf{...}` is a
/// `\\bfseries` inside a group the primitive opens and closes itself, which is
/// exactly what LaTeX's own definition of it says, so writing it that way is
/// one implementation rather than two that have to agree.
#include "render/primitives/styles.hpp"
#include "logger.hpp"

#include "syntax/semantics/scope.hpp"

#include <array>
#include <charconv>
#include <optional>
#include <system_error>
#include <algorithm>
#include <cmath>
#include <format>
#include <span>
#include <string>

namespace render::primitives {

    const typography::Font* Styles::resolve(const Context& context, const Cut cut, const float size) {
        const typography::Font* current = context.selection.text();
        if (!current) return nullptr;

        // The face in hand, read from its file's name: `lmromanslant10-bold`
        // is the roman family, bold, slanted. A family that does not name its
        // cuts the Latin Modern way is kept by its name up to its cut, and
        // whether its cuts are written with capitals.
        enum class Family : std::uint8_t { Roman, Sans, Mono, Other };
        enum class Shape : std::uint8_t { Upright, Italic, Slanted, Caps };

        const std::string_view name = current->family();
        const std::size_t dash = name.find_last_of('-');
        const std::string_view written = dash == std::string_view::npos ? std::string_view{} : name.substr(dash + 1);
        const auto says = [written](const std::string_view part) {
            return !std::ranges::search(written, part, [](const char have, const char want) {
                        return static_cast<char>(have >= 'A' && have <= 'Z' ? have + 32 : have) == want;
                    }).empty();
        };

        const std::size_t digit = name.find_first_of("0123456789");
        const std::string_view prefix = name.substr(0, digit == std::string_view::npos ? name.size() : digit);
        Family family = prefix.starts_with("lmroman") ? Family::Roman
                        : prefix.starts_with("lmsans") ? Family::Sans
                        : prefix.starts_with("lmmono") ? Family::Mono
                                                       : Family::Other;
        const std::string_view stem = dash == std::string_view::npos ? name : name.substr(0, dash);
        const bool capital = !written.empty() && written.front() >= 'A' && written.front() <= 'Z';
        bool bold = says("bold");
        Shape shape = prefix.find("caps") != std::string_view::npos    ? Shape::Caps
                      : prefix.find("slant") != std::string_view::npos ? Shape::Slanted
                      : says("italic") || says("oblique")             ? Shape::Italic
                                                                       : Shape::Upright;

        switch (cut) {
            case Cut::Current: break;
            case Cut::Normal:
                if (family != Family::Other) family = context.sans ? Family::Sans : Family::Roman;
                bold = false;
                shape = Shape::Upright;
                break;
            case Cut::Roman: if (family != Family::Other) family = Family::Roman; break;
            case Cut::Sans: if (family != Family::Other) family = Family::Sans; break;
            case Cut::Mono:
                // Another family has no typewriter of its own to find; Latin
                // Modern's is the one a document means.
                family = Family::Mono;
                break;
            case Cut::Medium: bold = false; break;
            case Cut::Bold: bold = true; break;
            case Cut::Upright: shape = Shape::Upright; break;
            case Cut::Italic: shape = Shape::Italic; break;
            case Cut::Slanted: shape = Shape::Slanted; break;
            case Cut::Caps: shape = Shape::Caps; break;
            case Cut::Emphasis:
                shape = shape == Shape::Italic || shape == Shape::Slanted ? Shape::Upright : Shape::Italic;
                break;
        }

        // The file the cut is filed under, written into a buffer here rather
        // than built as a string: this runs at every change of style. Latin
        // Modern's stem and cut, at the design size LaTeX's own font
        // definitions pick for the size, then at the 10-point design every
        // cut has; another family's stem and its cut, spelled its way.
        const bool slanted = shape == Shape::Italic || shape == Shape::Slanted;
        std::string_view base;
        std::string_view ending;
        if (family == Family::Other) {
            base = stem;
            ending = capital ? (bold && slanted ? "BoldItalic" : bold ? "Bold" : slanted ? "Italic" : "Regular")
                             : (bold && slanted ? "bolditalic" : bold ? "bold" : slanted ? "italic" : "regular");
        } else if (family == Family::Roman) {
            base = shape == Shape::Slanted ? "lmromanslant" : shape == Shape::Caps ? "lmromancaps" : "lmroman";
            ending = shape == Shape::Caps ? "regular"
                     : shape == Shape::Italic ? (bold ? "bolditalic" : "italic")
                                              : (bold ? "bold" : "regular");
        } else if (family == Family::Sans) {
            base = "lmsans";
            ending = slanted ? (bold ? "boldoblique" : "oblique") : (bold ? "bold" : "regular");
        } else if (bold) {
            base = "lmmonolt";
            ending = slanted ? "boldoblique" : "bold";
        } else {
            base = shape == Shape::Slanted ? "lmmonoslant" : shape == Shape::Caps ? "lmmonocaps" : "lmmono";
            ending = shape == Shape::Italic ? "italic" : "regular";
        }

        const int optical = size < 5.5f   ? 5
                            : size < 6.5f ? 6
                            : size < 7.5f ? 7
                            : size < 8.5f ? 8
                            : size < 9.5f ? 9
                            : size < 11.0f ? 10
                            : size < 15.0f ? 12
                                           : 17;
        std::array<char, 128> buffer{};
        for (const int design : {optical, 10}) {
            const auto end = family == Family::Other
                                 ? std::format_to_n(buffer.data(), buffer.size(), "{}-{}", base, ending).out
                                 : std::format_to_n(buffer.data(), buffer.size(), "{}{}-{}", base, design, ending).out;
            const std::string_view file(buffer.data(), static_cast<std::size_t>(end - buffer.data()));
            if (const typography::Font* found = context.registry.get({.family = file, .size = size})) return found;
            if (family == Family::Other || optical == 10) break;
        }

        // The tree has no such cut. Keeping the face in hand at the new size
        // is still better than dropping the size change with it.
        return context.registry.get({.family = current->family(), .size = size});
    }

    float Styles::measure(const float body, const Size step) noexcept {
        // LaTeX's size10.clo, size11.clo and size12.clo, \tiny to \Huge.
        static constexpr std::array<std::array<float, 10>, 3> tables{{
            {5.0f, 7.0f, 8.0f, 9.0f, 10.0f, 12.0f, 14.4f, 17.28f, 20.74f, 24.88f},
            {6.0f, 8.0f, 9.0f, 10.0f, 10.95f, 12.0f, 14.4f, 17.28f, 20.74f, 24.88f},
            {6.0f, 8.0f, 10.0f, 10.95f, 12.0f, 14.4f, 17.28f, 20.74f, 24.88f, 24.88f},
        }};
        static constexpr std::array<float, 3> bodies{10.0f, 10.95f, 12.0f};

        std::size_t nearest = 0;
        for (std::size_t index = 1; index < bodies.size(); ++index) {
            if (std::abs(bodies[index] - body) < std::abs(bodies[nearest] - body)) nearest = index;
        }
        const auto& table = tables[nearest];
        return table[static_cast<std::size_t>(step)] * body / table[static_cast<std::size_t>(Size::Normal)];
    }

    Styles::Styles(syntax::Lexicon& lexicon) noexcept {
        lexicon.intern("\\textbf");
        lexicon.intern("\\bfseries");
    }

    void Styles::operator()(syntax::Parser& parser, Context& context) const {
        /// @brief One style: what it is called, and what it changes.
        struct Style {
            std::string_view bounded;   ///< The form that takes an argument.
            std::string_view running;   ///< The form that runs to the end of its group.
            Cut cut;                    ///< What it changes about the face, if anything.
            Size size;                  ///< The size it selects, when its cut is Cut::Current.
        };

        // The sizes keep whatever face is in use, so `\\bfseries\\large`
        // stays bold, and every change of face keeps the size in use.
        static constexpr std::array<Style, 21> styles{{
            {"\\textrm", "\\rmfamily", Cut::Roman, Size::Normal},
            {"\\textsf", "\\sffamily", Cut::Sans, Size::Normal},
            {"\\texttt", "\\ttfamily", Cut::Mono, Size::Normal},
            {"\\textmd", "\\mdseries", Cut::Medium, Size::Normal},
            {"\\textbf", "\\bfseries", Cut::Bold, Size::Normal},
            {"\\textup", "\\upshape", Cut::Upright, Size::Normal},
            {"\\textit", "\\itshape", Cut::Italic, Size::Normal},
            {"\\textsl", "\\slshape", Cut::Slanted, Size::Normal},
            {"\\textsc", "\\scshape", Cut::Caps, Size::Normal},
            {"\\emph", "\\em", Cut::Emphasis, Size::Normal},
            {"\\textnormal", "\\normalfont", Cut::Normal, Size::Normal},

            {"\\texttiny", "\\tiny", Cut::Current, Size::Tiny},
            {"\\textscript", "\\scriptsize", Cut::Current, Size::Script},
            {"\\textfootnote", "\\footnotesize", Cut::Current, Size::Footnote},
            {"\\textsmall", "\\small", Cut::Current, Size::Small},
            {"\\textnormalsize", "\\normalsize", Cut::Current, Size::Normal},
            {"\\textlarge", "\\large", Cut::Current, Size::Large},
            {"\\textLarge", "\\Large", Cut::Current, Size::Larger},
            {"\\textLARGE", "\\LARGE", Cut::Current, Size::Largest},
            {"\\texthuge", "\\huge", Cut::Current, Size::Huge},
            {"\\textHuge", "\\Huge", Cut::Current, Size::Hugest},
        }};

        // The face a style selects where it is written: a change of face at
        // the size in use, or the size asked for against the class's body.
        const auto select = [&context](const Cut cut, const Size size) -> const typography::Font* {
            const typography::Font* current = context.selection.text();
            if (!current) return nullptr;
            const float points = cut == Cut::Current ? measure(context.document.configuration.size, size)
                                                     : current->size();
            return resolve(context, cut, points);
        };

        for (const auto& [bounded, running, cut, size] : styles) {
            // The running form: selects, and lets the group it sits in undo
            // the selection when it closes.
            // Bound in the parser rather than the expander, so the text read
            // before it is set apart first and keeps the face it was read in.
            parser.bind(running, [this, &context, select, cut, size](syntax::Parser& parser) -> syntax::Node* {
                const typography::Font* font = select(cut, size);
                if (!font) {
                    tracebacks.emplace_back(syntax::Traceback::Type::Primitive,
                                             parser.mouth.lookahead().location,
                                             "A style needs a text face to change");
                    return nullptr;
                }
                context.selection.text(font);
                return nullptr;
            });

            // The bounded form: the same selection, around one argument.
            parser.bind(bounded, [this, &context, select, cut, size](syntax::Parser& parser) -> syntax::Node* {
                syntax::Mouth& mouth = parser.mouth;
                memory::Arena& arena = parser.arena;
                const memory::Location origin = mouth.lookahead().location;

                syntax::Token open = mouth.read();
                if (!open.is(syntax::Catcodes::Category::Group, '{')) {
                    if (!open.empty()) mouth.stream().inject(std::span{&open, 1});
                    tracebacks.emplace_back(syntax::Traceback::Type::Group, origin,
                                             "A style needs a brace group to set");
                    return directive(arena, nullptr, origin);
                }

                const typography::Font* restore = context.selection.text();
                if (const typography::Font* font = select(cut, size)) {
                    context.selection.text(font);
                }

                mouth.push(syntax::semantics::Scope::Type::Group);
                const memory::Slice<syntax::Node*> children = parser.parse('}');
                mouth.pop(syntax::semantics::Scope::Type::Group);

                // Marked while the style is still selected. The text is shaped
                // later, when the document is composed, by which time the
                // selection has been put back -- so the choice has to travel
                // with the text rather than be looked up again.
                stamp(children, context);
                context.selection.text(restore);

                // A group node, not a box: a box could not be broken across a
                // line, and a styled sentence has to be.
                return arena.compose<syntax::Node>(
                    syntax::Node::Type::Group, std::string_view{}, origin, children);
            });
        }

        // LaTeX's font selection by its parts, which take effect together at
        // \selectfont: `\fontsize{12}{14}\selectfont`, `\fontseries{b}`,
        // `\fontshape{it}`, `\fontfamily{cmss}`, and `\usefont` for all of
        // them at once. A family is known by the names LaTeX's own fonts go
        // by -- roman, sans and typewriter, in Computer Modern's and Latin
        // Modern's spellings and the common others -- and any other reads as
        // the roman the body is set in.
        const auto read = [](syntax::Mouth& mouth) {
            std::string text;
            for (const syntax::Token& token : mouth.argument({}, 0)) text += token.text;
            return text;
        };
        parser.mouth.bind("\\fontsize", [this, &context, read](syntax::Mouth& mouth) {
            const std::string size = read(mouth);
            static_cast<void>(read(mouth));   // the baseline, which the class's leading keeps
            float points = 0.0f;
            if (std::from_chars(size.data(), size.data() + size.size(), points).ec == std::errc{} && points > 0.0f) {
                pending.size = points;
            } else {
                pending.size = primitives::measure(size, mouth, context);
            }
        });
        parser.mouth.bind("\\fontfamily", [this, read](syntax::Mouth& mouth) {
            const std::string family = read(mouth);
            pending.family = family.contains("ss") || family == "phv" || family == "sans" ? Cut::Sans
                             : family.contains("tt") || family == "pcr"                   ? Cut::Mono
                                                                                           : Cut::Roman;
        });
        parser.mouth.bind("\\fontseries", [this, read](syntax::Mouth& mouth) {
            pending.series = read(mouth).starts_with('b') ? Cut::Bold : Cut::Medium;
        });
        parser.mouth.bind("\\fontshape", [this, read](syntax::Mouth& mouth) {
            const std::string shape = read(mouth);
            pending.shape = shape == "it" ? Cut::Italic : shape == "sl" ? Cut::Slanted : shape == "sc" ? Cut::Caps
                                                                                                  : Cut::Upright;
        });
        parser.mouth.bind("\\fontencoding", [read](syntax::Mouth& mouth) { static_cast<void>(read(mouth)); });
        parser.bind("\\selectfont", [this, &context](syntax::Parser&) -> syntax::Node* {
            const typography::Font* current = context.selection.text();
            if (!current) return nullptr;
            const float points = pending.size.value_or(current->size());
            for (const std::optional<Cut> cut : {pending.family, pending.series, pending.shape}) {
                if (!cut) continue;
                if (const typography::Font* font = resolve(context, *cut, points)) context.selection.text(font);
            }
            if (const typography::Font* font = resolve(context, Cut::Current, points)) context.selection.text(font);
            pending = {};
            return nullptr;
        });
        parser.mouth.bind("\\usefont", [this, read](syntax::Mouth& mouth) {
            static_cast<void>(read(mouth));   // the encoding
            const std::array<std::string, 3> parts{read(mouth), read(mouth), read(mouth)};
            const std::string& family = parts[0];
            pending.family = family.contains("ss") ? Cut::Sans : family.contains("tt") ? Cut::Mono : Cut::Roman;
            pending.series = parts[1].starts_with('b') ? Cut::Bold : Cut::Medium;
            pending.shape = parts[2] == "it" ? Cut::Italic : parts[2] == "sl" ? Cut::Slanted
                          : parts[2] == "sc" ? Cut::Caps : Cut::Upright;
            mouth.ingest("\\selectfont ");
        });

        // The normal face's family, as \familydefault names it when the
        // document starts -- `\renewcommand{\familydefault}{\sfdefault}`,
        // or beamer's class -- followed through the macros it names to the
        // family's own name: the body in it, and the normal face after.
        context.blocks.watch(
            "document",
            [&context](syntax::Mouth& mouth) {
                std::string written = "\\familydefault";
                for (int depth = 0; depth < 3 && written.starts_with('\\'); ++depth) {
                    const syntax::Mouth::Macro* macro = mouth.macro(mouth.lexicon.intern(written));
                    if (!macro) break;
                    written.clear();
                    for (const syntax::Token& token : macro->body) written += token.text;
                }
                context.sans = written.contains("ss") || written == "phv" || written == "sans";
                const typography::Font* current = context.selection.text();
                if (!context.sans || !current) return;
                if (const typography::Font* font = resolve(context, Cut::Normal, current->size())) {
                    context.selection.text(font);
                    context.document.furniture.face = font;
                }
            },
            {});

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound style primitives");
    }

}
