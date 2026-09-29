/// @file
/// @brief Heading primitives: `\\section` and the levels beneath it.
///
/// The four levels share one closure, told apart by which counter they
/// advance. Everything else about them -- resetting what is below, printing
/// the number, setting the title -- is the same at every level, so it is
/// written once.
#include "render/primitives/sections.hpp"
#include "render/primitives/colors.hpp"
#include "render/primitives/counters.hpp"
#include "render/primitives/references.hpp"
#include "render/primitives/styles.hpp"
#include "syntax/argument.hpp"
#include "layout/line.hpp"
#include "logger.hpp"

#include "syntax/semantics/scope.hpp"
#include "syntax/semantics/union.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace render::primitives {

    /// The size a heading is set at, by level: LaTeX's `\\huge`, `\\Large`,
    /// `\\large` and `\\normalsize`, which is what its classes give a chapter
    /// and the three section levels beneath it.
    static constexpr std::array<Styles::Size, Sections::levels> enlargement{
        Styles::Size::Huge, Styles::Size::Larger, Styles::Size::Large, Styles::Size::Normal};

    /// The space LaTeX's article class keeps above and below a heading, by
    /// level, in x-heights of the body: `\\@startsection`'s own numbers.
    static constexpr std::array<std::pair<float, float>, Sections::levels> spacing{{
        {11.6f, 9.3f}, {3.5f, 2.3f}, {3.25f, 1.5f}, {3.25f, 1.5f},
    }};

    /// The counter each level steps, and the one `\\part` keeps apart from them.
    static constexpr std::array<std::string_view, Sections::levels> counted{
        "chapter", "section", "subsection", "subsubsection"};

    Sections::Sections(syntax::Lexicon& lexicon) noexcept {
        // Interned now so that the first use of a heading does not pay for it
        // in the middle of a parse.
        lexicon.intern("\\chapter");
        lexicon.intern("\\section");
        lexicon.intern("\\subsection");
        lexicon.intern("\\subsubsection");
        lexicon.intern("\\part");
        lexicon.intern("\\appendix");
        lexicon.intern("\\tableofcontents");
        // No document can write these names: a colon ends a control word.
        marks = {lexicon.intern("\\tableofcontents:list"), lexicon.intern("\\listoffigures:list"),
                 lexicon.intern("\\listoftables:list"), lexicon.intern("\\printindex:list")};
    }

    void Sections::operator()(syntax::Parser& parser, Context& context) const {
        static constexpr std::array<std::string_view, levels> names{
            "\\chapter", "\\section", "\\subsection", "\\subsubsection"};

        // LaTeX's article numbering: a section on its own, each level below
        // within the one above, and a part in capital Roman numerals.
        if (context.counters) {
            syntax::Mouth& mouth = parser.mouth;
            context.counters->define(mouth, "part", {}, "\\Roman");
            context.counters->define(mouth, "chapter");
            context.counters->define(mouth, "section");
            context.counters->define(mouth, "subsection", "section");
            context.counters->define(mouth, "subsubsection", "subsection");
        }

        // One heading's line: its number and a quad, when it has one, then
        // its title, set in the heading's face; and after it, the rule LaTeX
        // keeps -- the first paragraph under a heading is not indented,
        // however many blank lines stand between.
        // A chapter's or a part's number stands on a line of its own above the
        // title instead -- `Chapter 1`, in its own face -- as the classes set
        // one, and a chapter starts a new page.
        const auto heading = [&context](syntax::Parser& parser, const typography::Font* font,
                                        const std::string& number, const std::pair<float, float> around,
                                        const memory::Location origin, const std::size_t listed,
                                        const std::string& brief, const std::string& above = {},
                                        const typography::Font* lead = nullptr,
                                        const bool fresh = false, const Look* look = nullptr) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            memory::Arena& arena = parser.arena;

            // The title is read as any text is, so a macro or a formula in
            // it sets properly; the heading's face is selected around it and
            // stamped on everything it holds, then put back.
            // In its look's color, when it has one: the title as it is read,
            // and its number once shaped.
            const layout::Node::Color* tint = context.selection.color();
            if (look && !look->color.empty()) {
                const graphics::Color color = Colors::resolve(look->color, context.variables);
                tint = arena.compose<layout::Node::Color>(layout::Node::Color{color.r, color.g, color.b, color.alpha});
            }
            const typography::Font* restore = context.selection.text();
            const layout::Node::Color* painted = context.selection.color();
            context.selection.text(font);
            context.selection.color(tint);
            mouth.push(syntax::semantics::Scope::Type::Group);
            const memory::Slice<syntax::Node*> title = parser.parse('}');
            mouth.pop(syntax::semantics::Scope::Type::Group);
            stamp(title, context);
            context.selection.text(restore);
            context.selection.color(painted);

            // A title in capitals, as `\\MakeUppercase` sets it.
            if (look && look->upper) {
                const auto capitals = [&arena](this const auto& self, const memory::Slice<syntax::Node*> children) -> void {
                    for (syntax::Node* child : children) {
                        if (!child) continue;
                        if (child->type == syntax::Node::Type::Text) {
                            std::string written(child->value);
                            for (char& letter : written) {
                                if (letter >= 'a' && letter <= 'z') letter = static_cast<char>(letter - 'a' + 'A');
                            }
                            child->value = arena.copy(written);
                        } else if (child->type == syntax::Node::Type::Group) {
                            self(child->nodes);
                        }
                    }
                };
                capitals(title);
            }

            // What \\nameref calls it: the title's words, as written.
            context.title.clear();
            const auto words = [&context](this const auto& self, const memory::Slice<syntax::Node*> children) -> void {
                for (const syntax::Node* child : children) {
                    if (!child) continue;
                    if (child->type == syntax::Node::Type::Text) context.title += child->value;
                    else if (child->type == syntax::Node::Type::Group) self(child->nodes);
                }
            };
            words(title);

            // Where it stands, for its page, and -- when it is numbered at a
            // level a table of contents lists -- the entry it makes there.
            const std::size_t index = context.anchors++;
            if (listed != static_cast<std::size_t>(-1)) {
                memory::Slice<syntax::Node*> shown = title;
                if (!brief.empty()) {
                    shown = arena.allocate<syntax::Node*>(1);
                    shown[0] = arena.compose<syntax::Node>(syntax::Node::Type::Text, arena.copy(brief), origin);
                }
                context.entries.push_back({"toc", listed, number, shown, index});
                // hyperref's bookmark for it, in the file's outline.
                if (context.variables.get("links")) {
                    context.document.metadata.bookmarks.push_back(
                        {listed, (number.empty() ? std::string{} : number + " ") + (brief.empty() ? context.title : brief),
                         index});
                }
            }

            std::vector<layout::Node*> nodes;
            if (!number.empty() && above.empty()) {
                const typography::Font* fonts[] = {font};
                const memory::Slice<layout::Node*> shaped =
                    context.shaper.shape(memory::Slice{fonts, 1uz}, arena.copy(number), {});
                nodes.insert(nodes.end(), shaped.begin(), shaped.end());
                if (tint) {
                    for (layout::Node* piece : shaped) layout::Typesetter::paint(piece, *tint);
                }

                auto* quad = arena.compose<layout::Node>(layout::Node::Type::Kern);
                quad->kern({.width = look && look->sep >= 0.0f ? look->sep : font->size()});
                nodes.push_back(quad);
            }
            for (const syntax::Node* child : title) gather(nodes, child, context);

            // Centred or flush right: the line as wide as the column, the
            // room left taken by glue at its edges.
            float target = 0.0f;
            if (look && look->align != 'l') {
                const auto fil = [&arena] {
                    auto* glue = arena.compose<layout::Node>(layout::Node::Type::Glue);
                    glue->glue({.stretch = 1.0f, .expand = layout::Node::Order::Fil});
                    return glue;
                };
                nodes.insert(nodes.begin(), fil());
                if (look->align == 'c') nodes.push_back(fil());
                target = context.document.column();
            }

            const memory::Slice<layout::Node*> row = arena.allocate<layout::Node*>(nodes.size());
            std::ranges::copy(nodes, row.begin());

            auto* suppress = arena.compose<layout::Node>(layout::Node::Type::Directive);
            suppress->directive({.command = layout::Node::Directive::Command::Suppress});

            // Measured in the body's x-height, as \\@startsection's skips
            // are, each able to give a little.
            const float ex = context.document.configuration.size * 0.4306f;
            const auto space = [&arena, ex](const float amount) {
                auto* glue = arena.compose<layout::Node>(layout::Node::Type::Glue);
                glue->glue({.width = amount * ex, .stretch = ex * 0.2f, .shrink = ex * 0.2f});
                return glue;
            };

            auto* anchor = arena.compose<layout::Node>(layout::Node::Type::Directive);
            anchor->directive({.command = layout::Node::Directive::Command::Anchor, .index = index});

            std::vector<syntax::Node*> parts;
            if (fresh) {
                auto* pause = arena.compose<layout::Node>(layout::Node::Type::Pause);
                pause->pause({.penalty = {.value = -10000, .flag = true}});
                parts.push_back(directive(arena, pause, origin, true));
            }
            parts.push_back(directive(arena, space(around.first), origin, true));
            if (!above.empty() && lead) {
                const typography::Font* fonts[] = {lead};
                const memory::Slice<layout::Node*> shaped =
                    context.shaper.shape(memory::Slice{fonts, 1uz}, arena.copy(above), {});
                parts.push_back(directive(arena, layout::Line::horizontal(arena, shaped, 0.0f), origin, true));
                auto* gap = arena.compose<layout::Node>(layout::Node::Type::Glue);
                gap->glue({.width = 20.0f});
                parts.push_back(directive(arena, gap, origin, true));
            }
            parts.push_back(directive(arena, layout::Line::horizontal(arena, row, target), origin, true));
            // titlesec's \titlerule: across the column, a little under the
            // title's deepest letter.
            if (look && look->rule > 0.0f) {
                auto* gap = arena.compose<layout::Node>(layout::Node::Type::Kern);
                gap->kern({.width = 0.2f * ex});
                auto* bar = arena.compose<layout::Node>(layout::Node::Type::Rule);
                bar->rule({.width = breadth(context), .height = look->rule, .color = tint ? *tint : layout::Node::Color{}});
                parts.push_back(directive(arena, gap, origin, true));
                parts.push_back(directive(arena, bar, origin, true));
            }
            parts.push_back(directive(arena, anchor, origin));
            parts.push_back(directive(arena, space(around.second), origin, true));
            parts.push_back(directive(arena, suppress, origin));
            const memory::Slice<syntax::Node*> slice = arena.allocate<syntax::Node*>(parts.size());
            std::ranges::copy(parts, slice.begin());
            return arena.compose<syntax::Node>(syntax::Node::Type::Group, std::string_view{}, origin, slice);
        };

        // The star and the opening brace, which every heading reads first:
        // whether it is numbered, and whether it has a title to set at all.
        const auto opening = [this](syntax::Mouth& mouth, const memory::Location origin, bool& numbered,
                                    std::string& brief) {
            syntax::Token next = mouth.read();
            numbered = next.text != "*";
            if (!numbered) next = mouth.read();

            // `\section[short]{long}`: the short form is what the table of
            // contents prints.
            if (next.is('[')) {
                mouth.stream().inject(std::span{&next, 1});
                for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                    brief += token.text == "~" ? std::string_view{" "} : token.text;
                }
                next = mouth.read();
            }
            while (next.category == syntax::Catcodes::Category::Space) next = mouth.read();

            if (!next.is(syntax::Catcodes::Category::Group, '{')) {
                if (!next.empty()) mouth.stream().inject(std::span{&next, 1});
                tracebacks.emplace_back(syntax::Traceback::Type::Group, origin,
                                         "A heading needs a brace group for its title");
                return false;
            }
            return true;
        };

        // Each level bound twice: under its name, which a document may
        // redefine, and under a name no document can write -- `\\section:native`
        // -- which \\@startsection reaches it by whatever \\section became.
        for (std::size_t level = 0; level < levels; ++level) {
            const auto handler = [this, &context, level, heading, opening](
                                     syntax::Parser& parser) -> syntax::Node* {
                syntax::Mouth& mouth = parser.mouth;
                memory::Arena& arena = parser.arena;
                const memory::Location origin = mouth.lookahead().location;

                bool numbered = true;
                std::string brief;
                if (!opening(mouth, origin, numbered, brief)) return directive(arena, nullptr, origin, true);

                // LaTeX's secnumdepth: levels below it are set unnumbered,
                // and listed in the contents all the same -- a book's
                // preface after \frontmatter -- where a starred one is not.
                const bool listing = numbered;
                if (context.counters && context.counters->contains("secnumdepth") &&
                    static_cast<int>(level) > context.counters->value("secnumdepth")) {
                    numbered = false;
                }

                if (!context.selection.text()) {
                    tracebacks.emplace_back(syntax::Traceback::Type::Primitive, origin,
                                             "A heading needs a text face to be set in");
                    return directive(arena, nullptr, origin, true);
                }

                // A document's first chapter makes it a book: from then on a
                // section is numbered within its chapter.
                if (level == 0 && !chaptered && context.counters) {
                    chaptered = true;
                    context.counters->define(mouth, "section", "chapter");
                }

                // The number, as the level's `\the` macro prints it. It is
                // also what a `\label` after the heading refers to.
                std::string number;
                if (numbered && context.counters) {
                    context.counters->step(counted[level]);
                    number = Counters::print(mouth, counted[level]);
                    context.anchor = number;
                    // After \\appendix, every level is an appendix to cleveref.
                    context.kind = appendix ? std::string("appendix") : std::string(names[level].substr(1));
                }

                Logger::log(Logger::Type::Layout, Logger::Level::Debug,
                            "Heading at level {}{}", level + 1, numbered ? "" : ", unnumbered");

                // Bold, and larger by level, from the family the body is in,
                // measured against the class's body size so a heading inside
                // `\\large` text is no larger than any other.
                const float body = context.document.configuration.size;
                const typography::Font* font =
                    Styles::resolve(context, Styles::Cut::Bold, Styles::measure(body, enlargement[level]));
                const std::size_t listed = listing ? level : static_cast<std::size_t>(-1);

                // A look of the class's own, or titlesec's: its face built
                // from the body's by each change in turn, at its size; its
                // number written as it says; its spacing, when it gives one.
                if (const Look& look = looks[level]; look.set) {
                    const float size = Styles::measure(body, look.size);
                    const typography::Font* restore = context.selection.text();
                    context.selection.text(Styles::resolve(context, Styles::Cut::Normal, size));
                    for (const Styles::Cut cut : look.cuts) {
                        if (const typography::Font* changed = Styles::resolve(context, cut, size)) {
                            context.selection.text(changed);
                        }
                    }
                    const typography::Font* face = context.selection.text();
                    context.selection.text(restore);
                    if (!face) face = font;

                    if (look.unlabelled) {
                        number.clear();
                    } else if (!number.empty() && !look.label.empty()) {
                        mouth.ingest(arena.copy("{" + look.label + "}"));
                        number = syntax::Argument::expanded(mouth);
                    }
                    const float ex = body * 0.4306f;
                    const std::pair<float, float> around{
                        look.spacing.first >= 0.0f ? look.spacing.first / ex : spacing[level].first,
                        look.spacing.second >= 0.0f ? look.spacing.second / ex : spacing[level].second};
                    if (look.display && !number.empty()) {
                        return heading(parser, face, number, around, origin, listed, brief, number, face, level == 0,
                                       &look);
                    }
                    return heading(parser, face, number, around, origin, listed, brief, {}, nullptr, level == 0, &look);
                }
                if (level != 0) return heading(parser, font, number, spacing[level], origin, listed, brief);

                // A chapter: on a new page, `Chapter 1` -- or `Appendix A` --
                // in \\huge above its title in \\Huge, as report and book set it.
                std::string above;
                if (!number.empty()) {
                    mouth.ingest(appendix ? "{\\appendixname}" : "{\\chaptername}");
                    above = syntax::Argument::expanded(mouth) + " " + number;
                }
                const typography::Font* title =
                    Styles::resolve(context, Styles::Cut::Bold, Styles::measure(body, Styles::Size::Hugest));
                return heading(parser, title, number, spacing[level], origin, listed, brief, above, font, true);
            };
            parser.bind(names[level], handler);
            parser.bind(std::string(names[level]) + ":native", handler);
        }

        // A level's look, as titlesec's \\titleformat gives it and the
        // classes' modules set theirs: `\\@heading{\\section}{shape}{format}
        // {label}{sep}{before}`. The format and what comes before the title
        // are read for what they change -- the face and size switches, the
        // alignment titlesec's \\filcenter and LaTeX's \\centering give, a
        // \\MakeUppercase -- the label kept to be written with each heading,
        // an empty one showing no number; `display` sets the number on a
        // line of its own. Its spacing is \\@headingspace's, as titlesec's
        // \\titlespacing: `{\\section}{left}{above}{below}`.
        const auto leveled = [](const std::string_view name) -> std::optional<std::size_t> {
            for (std::size_t level = 0; level < levels; ++level) {
                if (name == names[level]) return level;
            }
            return std::nullopt;
        };
        // The face, size, alignment and capitals a format's switches ask
        // for, read into a look -- and its color, and titlesec's rule under
        // it, `\titlerule` or `\titlerule[0.8pt]`.
        const auto scan = [](const std::vector<syntax::Token>& list, Look& look) {
            for (std::size_t at = 0; at < list.size(); ++at) {
                const std::string_view word = list[at].text;
                // What stands between the brackets or the braces after the
                // word, when they do.
                const auto enclosed = [&list, &at](const char open, const char close) {
                    std::string written;
                    if (at + 1 >= list.size() || !list[at + 1].is(open)) return written;
                    for (at += 2; at < list.size() && !list[at].is(close); ++at) written += list[at].text;
                    return written;
                };
                if (word == "\\color" || word == "\\textcolor") {
                    look.color = enclosed('{', '}');
                    continue;
                }
                if (word == "\\titlerule") {
                    const std::string thick = enclosed('[', ']');
                    look.rule = 0.4f;
                    std::from_chars(thick.data(), thick.data() + thick.size(), look.rule);
                    continue;
                }
                if (word == "\\bfseries" || word == "\\bf") look.cuts.push_back(Styles::Cut::Bold);
                else if (word == "\\itshape" || word == "\\it" || word == "\\em") look.cuts.push_back(Styles::Cut::Italic);
                else if (word == "\\scshape" || word == "\\sc") look.cuts.push_back(Styles::Cut::Caps);
                else if (word == "\\slshape" || word == "\\sl") look.cuts.push_back(Styles::Cut::Slanted);
                else if (word == "\\sffamily" || word == "\\sf") look.cuts.push_back(Styles::Cut::Sans);
                else if (word == "\\ttfamily" || word == "\\tt") look.cuts.push_back(Styles::Cut::Mono);
                else if (word == "\\mdseries") look.cuts.push_back(Styles::Cut::Medium);
                else if (word == "\\upshape") look.cuts.push_back(Styles::Cut::Upright);
                else if (word == "\\normalfont" || word == "\\rmfamily") look.cuts.clear();
                else if (word == "\\tiny") look.size = Styles::Size::Tiny;
                else if (word == "\\scriptsize") look.size = Styles::Size::Script;
                else if (word == "\\footnotesize") look.size = Styles::Size::Footnote;
                else if (word == "\\small") look.size = Styles::Size::Small;
                else if (word == "\\normalsize") look.size = Styles::Size::Normal;
                else if (word == "\\large") look.size = Styles::Size::Large;
                else if (word == "\\Large") look.size = Styles::Size::Larger;
                else if (word == "\\LARGE") look.size = Styles::Size::Largest;
                else if (word == "\\huge") look.size = Styles::Size::Huge;
                else if (word == "\\Huge") look.size = Styles::Size::Hugest;
                else if (word == "\\centering" || word == "\\filcenter" || word == "\\center") look.align = 'c';
                else if (word == "\\raggedleft" || word == "\\filleft") look.align = 'r';
                else if (word == "\\raggedright" || word == "\\filright") look.align = 'l';
                else if (word == "\\MakeUppercase" || word == "\\uppercase" || word == "\\MakeTextUppercase") look.upper = true;
            }
        };
        // A skip as a heading's space is written, its natural amount in
        // points: its stretch let go, its sign too -- \\@startsection writes
        // a negative one to say what follows is not indented -- and
        // titlesec's `*4` four ex.
        const auto natural = [&context](const std::string& written, syntax::Mouth& mouth) {
            const std::size_t cut = std::min(written.find("plus"), written.find("minus"));
            if (written.starts_with('*')) {
                float amount = 0.0f;
                std::from_chars(written.data() + 1, written.data() + written.size(), amount);
                return amount * context.document.configuration.size * 0.4306f;
            }
            return std::abs(measure(written.substr(0, cut), mouth, context));
        };

        parser.mouth.bind("\\@heading", [this, &context, leveled, scan](syntax::Mouth& mouth) {
            const std::string name = syntax::Argument::text(mouth);
            const std::string shape = syntax::Argument::text(mouth);
            const std::vector<syntax::Token> format = mouth.argument({}, 1);
            std::string label;
            for (const syntax::Token& token : mouth.argument({}, 1)) {
                label += token.text;
                if (token.text.size() > 1 && token.text.front() == '\\') label += ' ';
            }
            const std::string sep = syntax::Argument::text(mouth);
            const std::vector<syntax::Token> before = mouth.argument({}, 1);
            const auto level = leveled(name);
            if (!level) return;

            // The standard classes' size for the level, until the format
            // gives its own.
            Look look{.set = true};
            look.size = *level == 0 ? Styles::Size::Huge : *level == 1 ? Styles::Size::Larger
                        : *level == 2 ? Styles::Size::Large : Styles::Size::Normal;
            look.display = shape == "display";
            // A label of `*` keeps the number as the counter prints it, as
            // titlesec's starred \\titleformat does.
            look.unlabelled = label.find_first_not_of(" ") == std::string::npos;
            look.label = label.starts_with('*') ? std::string{} : label;
            if (!sep.empty()) look.sep = measure(sep, mouth, context);
            scan(format, look);
            scan(before, look);
            look.spacing = looks[*level].spacing;
            looks[*level] = std::move(look);
        });

        // The kernel's \\@startsection{name}{level}{indent}{before}{after}{style},
        // which a preamble redefines a heading as: the level's look from the
        // style and its spacing from the two skips, then the heading itself,
        // its star, short title and title read as the level reads them. A
        // level past subsubsection, or a heading of a name of its own below
        // it, runs in at the start of its paragraph, in its style.
        parser.mouth.bind("\\@startsection", [this, leveled, scan, natural](syntax::Mouth& mouth) {
            const std::string name = "\\" + syntax::Argument::text(mouth);
            const std::string depth = syntax::Argument::expanded(mouth);
            static_cast<void>(syntax::Argument::text(mouth));
            const std::string above = syntax::Argument::expanded(mouth);
            const std::string below = syntax::Argument::expanded(mouth);
            const std::vector<syntax::Token> style = mouth.argument({}, 1);

            std::optional<std::size_t> level = leveled(name);
            if (!level) {
                int number = 0;
                std::from_chars(depth.data(), depth.data() + depth.size(), number);
                if (number >= 0 && number < static_cast<int>(levels)) level = static_cast<std::size_t>(number);
            }
            if (!level || below.starts_with('-')) {
                std::string written;
                for (const syntax::Token& token : style) {
                    written += token.text;
                    if (token.text.size() > 1 && token.text.front() == '\\') written += ' ';
                }
                mouth.ingest(mouth.arena.copy("\\@runin{" + written + "}"));
                return;
            }
            if (leveled(name)) {
                Look look{.set = true, .size = Styles::Size::Normal};
                scan(style, look);
                look.spacing = {natural(above, mouth), natural(below, mouth)};
                looks[*level] = std::move(look);
            }
            const syntax::Symbol native = mouth.lexicon.intern(std::string(names[*level]) + ":native");
            const syntax::Token token{.symbol = native, .category = syntax::Catcodes::Category::Escape,
                                      .text = mouth.lexicon.resolve(native)};
            mouth.stream().inject(std::span{&token, 1});
        });

        parser.mouth.bind("\\@headingspace", [this, leveled, natural](syntax::Mouth& mouth) {
            const std::string name = syntax::Argument::text(mouth);
            static_cast<void>(syntax::Argument::text(mouth));
            const std::string above = syntax::Argument::text(mouth);
            const std::string below = syntax::Argument::text(mouth);
            const auto level = leveled(name);
            if (!level) return;
            Look& look = looks[*level];
            if (!look.set) {
                look.set = true;
                look.cuts = {Styles::Cut::Bold};
                look.size = enlargement[*level];
            }
            look.spacing = {natural(above, mouth), natural(below, mouth)};
        });

        // `\\part` stands outside the chain above: its own count, roman
        // numbered, resetting nothing beneath it -- a document's chapters
        // keep counting straight through a part break.
        parser.bind("\\part", [this, &context, heading, opening](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            memory::Arena& arena = parser.arena;
            const memory::Location origin = mouth.lookahead().location;

            bool numbered = true;
            std::string brief;
            if (!opening(mouth, origin, numbered, brief)) return directive(arena, nullptr, origin, true);

            if (!context.selection.text()) {
                tracebacks.emplace_back(syntax::Traceback::Type::Primitive, origin,
                                         "\\part needs a text face to be set in");
                return directive(arena, nullptr, origin, true);
            }

            // `Part I` in \\Large on a line of its own, the title under it in
            // \\huge, as the article class sets a part.
            std::string label;
            if (numbered && context.counters) {
                context.counters->step("part");
                const std::string number = Counters::print(mouth, "part");
                mouth.ingest("{\\partname}");
                label = syntax::Argument::expanded(mouth) + " " + number;
                context.anchor = number;
                context.kind = "part";
            }

            const float body = context.document.configuration.size;
            const typography::Font* font = Styles::resolve(context, Styles::Cut::Bold, Styles::measure(body, Styles::Size::Huge));
            const typography::Font* lead = Styles::resolve(context, Styles::Cut::Bold, Styles::measure(body, Styles::Size::Larger));
            return heading(parser, font, label, {4.0f, 3.0f}, origin, static_cast<std::size_t>(-1), brief, label, lead);
        });

        // From here the outermost numbered level -- a chapter if the
        // document has one, a section if it does not -- starts again and
        // letters instead of counting: Appendix A, and its A.1.
        parser.mouth.bind("\\appendix", [this, &context](syntax::Mouth& mouth) {
            if (!context.counters) return;
            const std::string_view outer = chaptered ? "chapter" : "section";
            context.counters->set(outer, 0);
            context.counters->define(mouth, outer, {}, "\\Alph");
            appendix = true;
        });

        // The appendix package's block: the same, for everything inside it.
        context.blocks.watch("appendices", [](syntax::Mouth& mouth) { mouth.ingest("\\appendix "); }, {});

        // The classes' acknowledgments: acmart's under an unnumbered heading,
        // revtex's and elsarticle's a paragraph after a little space, and
        // llncs's credits in a smaller size.
        static constexpr std::array<std::pair<std::string_view, std::string_view>, 4> thanks{{
            {"acks", "\\section*{\\acksname}"},
            {"acknowledgments", "\\par\\medskip "},
            {"acknowledgements", "\\par\\medskip "},
            {"credits", "\\par\\small "},
        }};
        for (const auto& [name, head] : thanks) {
            context.blocks.watch(name, [head](syntax::Mouth& mouth) { mouth.ingest(head); },
                                 [](syntax::Mouth& mouth) { mouth.ingest("\\par "); });
        }

        // The table of contents and the lists of figures and of tables: each
        // a heading of its own, then a place kept for its lines, which are
        // known only once every heading and caption has been read and are
        // set there as the document ends. Their pages are the ones the pass
        // before found, which is why they ask for one.
        static constexpr std::array<std::pair<std::string_view, std::string_view>, 3> named{{
            {"\\tableofcontents", "\\contentsname"},
            {"\\listoffigures", "\\listfigurename"},
            {"\\listoftables", "\\listtablename"},
        }};
        for (std::size_t which = 0; which < named.size(); ++which) {
            parser.bind(named[which].first, [this, &context, which](syntax::Parser& parser) -> syntax::Node* {
                syntax::Mouth& mouth = parser.mouth;
                if (!context.variables.get("contents.bare")) context.paged = true;
                const syntax::Token mark{.symbol = marks[which], .category = syntax::Catcodes::Category::Escape,
                                         .text = mouth.lexicon.resolve(marks[which])};
                mouth.stream().inject(std::span{&mark, 1});
                mouth.ingest(parser.arena.copy(std::format("\\{}*{{{}}}", chaptered ? "chapter" : "section",
                                                             named[which].second)));
                return nullptr;
            });
            parser.bind(marks[which], [this, which](syntax::Parser& parser) -> syntax::Node* {
                auto* place = parser.arena.compose<syntax::Node>(syntax::Node::Type::Group, std::string_view{},
                                                                   parser.mouth.lookahead().location);
                places.emplace_back(which, place);
                return place;
            });
        }

        // \addcontentsline{toc}{section}{text}: a line of a list's own, for a
        // heading that makes none -- an unnumbered one -- set where it is
        // written, so its page is the one it lands on.
        parser.bind("\\addcontentsline", [&context](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            memory::Arena& arena = parser.arena;
            const memory::Location origin = mouth.lookahead().location;
            const std::string list = syntax::Argument::text(mouth);
            const std::string kind = syntax::Argument::text(mouth);
            syntax::Token open = mouth.read();
            while (open.category == syntax::Catcodes::Category::Space) open = mouth.read();
            if (!open.is(syntax::Catcodes::Category::Group, '{')) {
                if (!open.empty()) mouth.stream().inject(std::span{&open, 1});
                return nullptr;
            }
            mouth.push(syntax::semantics::Scope::Type::Group);
            const memory::Slice<syntax::Node*> title = parser.parse('}');
            mouth.pop(syntax::semantics::Scope::Type::Group);

            const std::size_t level = kind == "part" || kind == "chapter" ? 0 : kind == "subsection" ? 2
                                    : kind == "subsubsection" || kind == "paragraph"                ? 3 : 1;
            const std::size_t index = context.anchors++;
            context.entries.push_back({list, level, {}, title, index});
            if (list == "toc" && context.variables.get("links")) {
                std::string words;
                [&words](this const auto& self, const memory::Slice<syntax::Node*> children) -> void {
                    for (const syntax::Node* child : children) {
                        if (child && child->type == syntax::Node::Type::Text) words += child->value;
                        else if (child && child->type == syntax::Node::Type::Group) self(child->nodes);
                    }
                }(title);
                context.document.metadata.bookmarks.push_back({level, std::move(words), index});
            }
            auto* anchor = arena.compose<layout::Node>(layout::Node::Type::Directive);
            anchor->directive({.command = layout::Node::Directive::Command::Anchor, .index = index});
            return directive(arena, anchor, origin);
        });

        // makeidx's index: each \index{entry} an anchor where it stands, read
        // as makeindex reads one -- `key@shown` sorts by the key and prints
        // what follows the @, `a!b` is b under a, and after a `|` how its
        // page is set: `textbf`, `textit`, `see{...}`, `seealso{...}`, and a
        // range's `(` and `)`. imakeidx's index name is read and let go.
        parser.bind("\\index", [this, &context](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            memory::Arena& arena = parser.arena;
            const memory::Location origin = mouth.lookahead().location;
            static_cast<void>(mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0));
            std::string written;
            for (const syntax::Token& token : mouth.argument({}, 0)) {
                written += token.text;
                if (token.text.size() > 1 && token.text.front() == '\\' && std::isalpha(static_cast<unsigned char>(token.text.back()))) {
                    written += ' ';
                }
            }
            // The text cut where one of the characters stands outside braces.
            const auto cut = [](const std::string_view text, const char mark) {
                std::vector<std::string_view> found;
                int depth = 0;
                std::size_t begin = 0;
                for (std::size_t at = 0; at <= text.size(); ++at) {
                    if (at < text.size()) {
                        if (text[at] == '{') ++depth;
                        if (text[at] == '}') --depth;
                        if (text[at] != mark || depth != 0 || (at > 0 && text[at - 1] == '"')) continue;
                    }
                    found.push_back(text.substr(begin, at - begin));
                    begin = at + 1;
                }
                return found;
            };
            const auto trim = [](std::string_view text) {
                while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
                while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
                return std::string(text);
            };

            Term term{.anchor = context.anchors++};
            const std::vector<std::string_view> sides = cut(written, '|');
            if (sides.size() > 1) term.encap = trim(sides[1]);
            for (const std::string_view level : cut(sides.front(), '!')) {
                const std::vector<std::string_view> parts = cut(level, '@');
                term.keys.push_back(trim(parts.front()));
                mouth.ingest(arena.copy("{" + trim(parts.back()) + "}"));
                mouth.read();
                mouth.push(syntax::semantics::Scope::Type::Group);
                const memory::Slice<syntax::Node*> shown = parser.parse('}');
                mouth.pop(syntax::semantics::Scope::Type::Group);
                stamp(shown, context);
                term.shown.push_back(shown);
            }
            terms.push_back(std::move(term));

            auto* anchor = arena.compose<layout::Node>(layout::Node::Type::Directive);
            anchor->directive({.command = layout::Node::Directive::Command::Anchor, .index = terms.back().anchor});
            return directive(arena, anchor, origin);
        });

        // The index itself, as LaTeX's theindex sets it: on a page of its own
        // in two columns under an unnumbered heading, \indexname, and what
        // follows it on a new page; its lines known once every page is.
        parser.bind("\\printindex", [this, &context](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            static_cast<void>(mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0));
            if (terms.empty()) return nullptr;
            context.paged = true;
            mouth.ingest("\\onecolumn ");
            const syntax::Token mark{.symbol = marks[3], .category = syntax::Catcodes::Category::Escape,
                                     .text = mouth.lexicon.resolve(marks[3])};
            mouth.stream().inject(std::span{&mark, 1});
            mouth.ingest("\\twocolumn[\\csname\\@bibkind\\endcsname*{\\indexname}]");
            return nullptr;
        });
        parser.bind(marks[3], [this](syntax::Parser& parser) -> syntax::Node* {
            auto* place = parser.arena.compose<syntax::Node>(syntax::Node::Type::Group, std::string_view{},
                                                               parser.mouth.lookahead().location);
            places.emplace_back(3, place);
            return place;
        });

        // One line per entry, as LaTeX's classes set it: the outermost level
        // listed bold, with space above and no leaders; each level beneath
        // indented further, its number in a column of its own, dots led
        // across to the page. A title too long for the line runs over the
        // leaders rather than onto a second line.
        context.blocks.watch("document", {}, [this, &context](syntax::Mouth&) {
            if (places.empty()) return;
            memory::Arena& arena = context.arena;
            const float size = context.document.configuration.size;
            const typography::Font* regular = Styles::resolve(context, Styles::Cut::Normal, size);
            const typography::Font* bold = Styles::resolve(context, Styles::Cut::Bold, size);
            if (!regular || !bold) return;
            const float width = static_cast<float>(context.registers.get(
                                    syntax::semantics::Registers::Type::Dimension,
                                    syntax::semantics::Registers::reserved + 1)) / 65536.0f;

            const auto shaped = [&](const typography::Font* face, const std::string_view written) {
                const typography::Font* fonts[] = {face};
                return context.shaper.shape(memory::Slice{fonts, 1uz}, arena.copy(written), {});
            };
            const auto kern = [&](const float amount) {
                auto* node = arena.compose<layout::Node>(layout::Node::Type::Kern);
                node->kern({.width = amount});
                return node;
            };
            const auto fill = [&](const layout::Node* leader) {
                auto* node = arena.compose<layout::Node>(layout::Node::Type::Glue);
                node->glue({.stretch = 1.0f, .expand = layout::Node::Order::Fill, .leader = leader});
                return node;
            };
            const auto boxed = [&](std::vector<layout::Node*> nodes, const float target) {
                const memory::Slice<layout::Node*> row = arena.allocate<layout::Node*>(nodes.size());
                std::ranges::copy(nodes, row.begin());
                return layout::Line::horizontal(arena, row, target);
            };

            // A dot with four and a half mu either side, as \\@dotsep leaves it.
            std::vector<layout::Node*> dotted{kern(size * 0.25f)};
            for (layout::Node* piece : shaped(regular, ".")) dotted.push_back(piece);
            dotted.push_back(kern(size * 0.25f));
            const layout::Node* dots = boxed(dotted, 0.0f);

            // Indent and number column, in ems, by how far below the
            // outermost listed level an entry is: \\l@section and the rest.
            static constexpr std::array<std::pair<float, float>, levels> shapes{{
                {0.0f, 1.5f}, {1.5f, 2.3f}, {3.8f, 3.2f}, {7.0f, 4.1f},
            }};

            // LaTeX's tocdepth: levels below it are left out of the contents.
            const int deepest = context.counters && context.counters->contains("tocdepth")
                                    ? context.counters->value("tocdepth") : 3;
            // A class that lists its headings without their pages -- beamer,
            // whose contents are an outline -- says so, and they are set in
            // the normal face with no leaders and no page.
            const bool bare = context.variables.get("contents.bare") != nullptr;
            static constexpr std::array<std::string_view, 3> lists{"toc", "lof", "lot"};
            for (const auto& [which, place] : places) {
            if (which >= lists.size()) continue;
            std::vector<syntax::Node*> parts;
            const memory::Location origin{};
            for (const Context::Entry& entry : context.entries) {
                if (entry.list != lists[which]) continue;
                const bool contents = which == 0;
                if (contents && static_cast<int>(entry.level) > deepest) continue;
                const std::size_t depth = contents ? entry.level - (chaptered ? 0 : std::min<std::size_t>(entry.level, 1))
                                                   : 1;
                const bool top = contents && depth == 0;
                const typography::Font* face = top && !bare ? bold : regular;

                std::vector<layout::Node*> line{kern(shapes[depth].first * size)};
                std::vector<layout::Node*> number;
                for (layout::Node* piece : shaped(face, entry.number)) number.push_back(piece);
                number.push_back(fill(nullptr));
                if (!entry.number.empty()) line.push_back(boxed(number, shapes[depth].second * size));

                // The title's words again, in the entry's face; anything
                // else it held -- a formula -- as it was.
                const auto retitle = [&](this const auto& self, const memory::Slice<syntax::Node*> children) -> void {
                    for (const syntax::Node* child : children) {
                        if (!child || child->type == syntax::Node::Type::Directive) continue;
                        if (child->type == syntax::Node::Type::Group) {
                            self(child->nodes);
                            continue;
                        }
                        syntax::Node copy = *child;
                        if (copy.type == syntax::Node::Type::Text) copy.face = face;
                        gather(line, &copy, context);
                    }
                };
                retitle(entry.title);

                line.push_back(fill(top || bare ? nullptr : dots));
                if (!bare) {
                    const std::string page = context.folios && entry.anchor < context.folios->size()
                                                 ? (*context.folios)[entry.anchor] : std::string{};
                    std::vector<layout::Node*> folio{fill(nullptr)};
                    for (layout::Node* piece : shaped(face, page)) folio.push_back(piece);
                    line.push_back(boxed(folio, 1.55f * size));
                }

                // With hyperref, the line goes to its heading, in linkcolor
                // with `colorlinks`.
                if (const References::Hyperlink link = References::hyperlink(context, {}, entry.anchor, "link");
                    link.open) {
                    if (link.tint) {
                        for (layout::Node* piece : line) layout::Typesetter::paint(piece, *link.tint);
                    }
                    line.insert(line.begin(), link.open);
                    line.push_back(link.close);
                }

                if (top) {
                    auto* above = arena.compose<layout::Node>(layout::Node::Type::Glue);
                    above->glue({.width = size, .stretch = 1.0f});
                    parts.push_back(directive(arena, above, origin, true));
                }
                parts.push_back(directive(arena, boxed(line, width), origin, true));
            }

            const memory::Slice<syntax::Node*> nodes = arena.allocate<syntax::Node*>(parts.size());
            std::ranges::copy(parts, nodes.begin());
            place->nodes = nodes;
            }

            // The index, as makeindex sorts and sets one: level by level,
            // letters regardless of their case, symbols before figures before
            // letters, entries alike kept in the order written; a line each,
            // a level beneath indented further, its pages after it -- three
            // or more in a row, and a `(` to its `)`, as a range -- and a
            // little space between one initial and the next.
            const auto order = [](const std::string_view key) {
                std::string sorted;
                for (const char letter : key) {
                    const auto code = static_cast<unsigned char>(letter);
                    sorted += std::isalpha(code) ? '2' : std::isdigit(code) ? '1' : '0';
                    sorted += static_cast<char>(std::tolower(code));
                }
                return sorted;
            };
            for (const auto& [which, place] : places) {
                if (which != 3) continue;
                std::vector<const Term*> sorted;
                for (const Term& term : terms) sorted.push_back(&term);
                const auto path = [&order](const Term* term) {
                    std::vector<std::string> keys;
                    for (const std::string& key : term->keys) keys.push_back(order(key));
                    return keys;
                };
                std::ranges::stable_sort(sorted, {}, path);

                const typography::Font* italic = Styles::resolve(context, Styles::Cut::Italic, size);
                const auto folio = [&context](const std::size_t anchor) {
                    return context.folios && anchor < context.folios->size() ? (*context.folios)[anchor] : std::string{};
                };
                std::vector<syntax::Node*> parts;
                std::vector<std::string> printed;
                const memory::Location origin{};
                for (std::size_t at = 0; at < sorted.size();) {
                    std::size_t end = at + 1;
                    while (end < sorted.size() && path(sorted[end]) == path(sorted[at])) ++end;
                    const std::vector<std::string> keys = path(sorted[at]);

                    if (!printed.empty() && !keys.empty() && !printed.front().empty() && keys.front().size() > 1 &&
                        printed.front().substr(0, 2) != keys.front().substr(0, 2)) {
                        auto* gap = arena.compose<layout::Node>(layout::Node::Type::Glue);
                        gap->glue({.width = 10.0f, .stretch = 5.0f, .shrink = 3.0f});
                        parts.push_back(directive(arena, gap, origin, true));
                    }
                    for (std::size_t level = 0; level < keys.size(); ++level) {
                        const bool last = level + 1 == keys.size();
                        if (!last && level < printed.size() && printed[level] == keys[level]) continue;
                        std::vector<layout::Node*> line{kern(static_cast<float>(level) * 20.0f)};
                        for (const syntax::Node* child : sorted[at]->shown[level]) gather(line, child, context);

                        // Its pages, each in the face its encap asks for, and
                        // what it sends a reader to see.
                        if (last) {
                            std::vector<std::pair<std::string, const typography::Font*>> pages;
                            std::string see;
                            std::string opened;
                            for (std::size_t index = at; index < end; ++index) {
                                const std::string& encap = sorted[index]->encap;
                                const std::string page = folio(sorted[index]->anchor);
                                if (encap.starts_with("see")) {
                                    const std::size_t brace = encap.find('{');
                                    std::string target = brace == std::string::npos ? std::string{} : encap.substr(brace + 1);
                                    if (target.ends_with('}')) target.pop_back();
                                    see = (encap.starts_with("seealso") ? "see also " : "see ") + target;
                                } else if (encap == "(") {
                                    opened = page;
                                } else if (encap == ")" && !opened.empty()) {
                                    pages.emplace_back(opened == page ? page : opened + "\xE2\x80\x93" + page, regular);
                                    opened.clear();
                                } else if (pages.empty() || pages.back().first != page) {
                                    pages.emplace_back(page, encap.contains("bf") ? bold : encap.contains("it") ||
                                                                encap.contains("emph") ? italic : regular);
                                }
                            }
                            // Three or more pages in a row, one range.
                            std::vector<std::pair<std::string, const typography::Font*>> joined;
                            const auto whole = [](const std::string& page) {
                                int value = 0;
                                const auto [stop, failure] = std::from_chars(page.data(), page.data() + page.size(), value);
                                return failure == std::errc{} && stop == page.data() + page.size() ? value : -1;
                            };
                            for (std::size_t index = 0; index < pages.size();) {
                                std::size_t run = index + 1;
                                while (run < pages.size() && whole(pages[run].first) >= 0 &&
                                       whole(pages[run].first) == whole(pages[run - 1].first) + 1 &&
                                       pages[run].second == pages[index].second) {
                                    ++run;
                                }
                                if (run - index >= 3 && whole(pages[index].first) >= 0) {
                                    joined.emplace_back(pages[index].first + "\xE2\x80\x93" + pages[run - 1].first,
                                                        pages[index].second);
                                    index = run;
                                } else {
                                    joined.push_back(pages[index++]);
                                }
                            }
                            for (const auto& [page, face] : joined) {
                                for (layout::Node* piece : shaped(regular, ", ")) line.push_back(piece);
                                for (layout::Node* piece : shaped(face ? face : regular, page)) line.push_back(piece);
                            }
                            if (!see.empty()) {
                                for (layout::Node* piece : shaped(regular, ", ")) line.push_back(piece);
                                const std::size_t split = see.find(' ', see.starts_with("see also") ? 4 : 0);
                                for (layout::Node* piece : shaped(italic ? italic : regular, see.substr(0, split))) {
                                    line.push_back(piece);
                                }
                                for (layout::Node* piece : shaped(regular, see.substr(split))) line.push_back(piece);
                            }
                        }
                        parts.push_back(directive(arena, boxed(line, 0.0f), origin, true));
                    }
                    printed = keys;
                    at = end;
                }
                const memory::Slice<syntax::Node*> nodes = arena.allocate<syntax::Node*>(parts.size());
                std::ranges::copy(parts, nodes.begin());
                place->nodes = nodes;
            }
        });

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound heading primitives");
    }

}
