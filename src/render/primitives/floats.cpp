/// @file
/// @brief Float implementation: the blocks, and a caption as a paragraph of
///        its own.
#include "render/primitives/floats.hpp"
#include "render/primitives/counters.hpp"
#include "render/primitives/styles.hpp"
#include "syntax/argument.hpp"
#include "syntax/semantics/scope.hpp"
#include "layout/line.hpp"
#include "logger.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <tuple>
#include <string_view>
#include <utility>
#include <vector>

namespace render::primitives {

    Floats::Floats(syntax::Lexicon& lexicon) noexcept {
        lexicon.intern("\\caption");
        lexicon.intern("\\captionof");
        hold = lexicon.intern("\\float:hold");
        fixed = lexicon.intern("\\float:fixed");
        release = lexicon.intern("\\float:release");
    }

    syntax::Node* Floats::caption(syntax::Parser& parser, Context& context, const std::string& kind,
                                  const bool ruled) const {
        syntax::Mouth& mouth = parser.mouth;
        memory::Arena& arena = parser.arena;
        const memory::Location origin = mouth.lookahead().location;

        // `\caption[short]{long}`: the short form is what the list of
        // figures or of tables prints.
        std::string brief;
        for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
            brief += token.text == "~" ? std::string_view{" "} : token.text;
        }

        syntax::Token open = mouth.read();
        while (open.category == syntax::Catcodes::Category::Space) open = mouth.read();
        if (!open.is(syntax::Catcodes::Category::Group, '{')) {
            if (!open.empty()) mouth.stream().inject(std::span{&open, 1});
            tracebacks.emplace_back(syntax::Traceback::Type::Group, origin, "\\caption needs a brace group");
            return nullptr;
        }

        const typography::Font* restore = context.selection.text();
        if (!restore) {
            tracebacks.emplace_back(syntax::Traceback::Type::Primitive, origin,
                                     "\\caption needs a text face to be set in");
            return nullptr;
        }

        // Its number, by its float's counter and printed through its \the;
        // its name, through the macro a document renames it with.
        std::string number;
        if (context.counters) {
            context.counters->step(kind);
            number = Counters::print(mouth, kind);
        }
        context.anchor = number;
        context.kind = kind;

        // A subfigure's or a subtable's: its letter in parentheses and no
        // name, and a reference to it the float's number and the letter --
        // the number the float's own caption will give it, when that is
        // still to come, as it usually is.
        const bool sub = kind.starts_with("sub");
        if (sub && context.counters) {
            const std::string parent = kind.substr(3);
            const bool ahead = floats.size() < 2 || !floats[floats.size() - 2].captioned;
            const int value = context.counters->value(parent);
            if (ahead) context.counters->set(parent, value + 1);
            context.anchor = Counters::print(mouth, parent) + number;
            if (ahead) context.counters->set(parent, value);
            number = "(" + number + ")";
        } else if (!floats.empty()) {
            floats.back().captioned = true;
        }

        // Named by `\\<kind>name`, as LaTeX names its own -- \\figurename,
        // \\tablename -- and the float package a float of a new kind.
        const std::string macro = "\\" + kind + "name";
        std::string name;
        if (!sub) {
            mouth.ingest(context.arena.copy("{" + macro + "}"));
            name = syntax::Argument::expanded(mouth);
            if (name == macro) name = kind == "algorithm" ? "Algorithm" : name;
        }

        // The caption package's settings: the label's weight, the caption's
        // size, and what stands between the two.
        const std::string* labelfont = context.variables.get("caption.labelfont");
        const std::string* font = context.variables.get("caption.font");
        const std::string* labelsep = context.variables.get("caption.labelsep");
        const bool bold = ruled || (labelfont && labelfont->find("bf") != std::string::npos);
        std::string separator = ruled || sub ? " " : ": ";
        if (labelsep && *labelsep == "period") separator = ". ";
        if (labelsep && (*labelsep == "space" || *labelsep == "quad")) separator = " ";
        if (labelsep && *labelsep == "newline") separator = " ";

        const layout::Document::Configuration& page = context.document.configuration;
        Styles::Size step = Styles::Size::Normal;
        if (font && font->find("small") != std::string::npos) step = Styles::Size::Small;
        if (font && font->find("footnotesize") != std::string::npos) step = Styles::Size::Footnote;
        if (font && font->find("scriptsize") != std::string::npos) step = Styles::Size::Script;
        const float size = Styles::measure(page.size, step);

        const typography::Font* face = Styles::resolve(context, Styles::Cut::Current, size);
        if (!face) face = restore;
        if (font && font->find("it") != std::string::npos) {
            context.selection.text(face);
            face = Styles::resolve(context, Styles::Cut::Italic, size);
        }
        context.selection.text(face);
        const typography::Font* heavy = bold ? Styles::resolve(context, Styles::Cut::Bold, size) : face;

        mouth.push(syntax::semantics::Scope::Type::Group);
        const memory::Slice<syntax::Node*> body = parser.parse('}');
        mouth.pop(syntax::semantics::Scope::Type::Group);
        stamp(body, context);

        // Measured once, so a caption that fits on one line is centred and
        // a longer one justified, as the standard classes decide.
        std::vector<layout::Node*> measured;
        for (const syntax::Node* child : body) compose(measured, child, context);
        context.selection.text(restore);

        const typography::Font* labelled[] = {heavy ? heavy : face};
        const std::string label = name.empty() ? number : name + (number.empty() ? "" : " " + number);
        const memory::Slice<layout::Node*> shaped =
            context.shaper.shape(memory::Slice{labelled, 1uz}, arena.copy(label), {});
        layout::Node* head = layout::Line::horizontal(arena, shaped, 0.0f);
        const memory::Slice<layout::Node*> spaced =
            context.shaper.shape(memory::Slice{labelled, 1uz}, arena.copy(separator), {});
        layout::Node* gap = layout::Line::horizontal(arena, spaced, 0.0f);

        float width = head->box().width + gap->box().width;
        for (const layout::Node* node : measured) width += layout::Line::advance(node);
        const float room = static_cast<float>(context.registers.get(syntax::semantics::Registers::Type::Dimension,
                                                                    syntax::semantics::Registers::reserved + 1)) / 65536.0f;
        const bool single = width <= room;

        using Command = layout::Node::Directive::Command;
        const auto order = [&arena, origin](const layout::Node::Directive& value) {
            auto* node = arena.compose<layout::Node>(layout::Node::Type::Directive);
            node->directive(value);
            return directive(arena, node, origin);
        };
        const auto space = [&arena, origin](const float amount) {
            auto* glue = arena.compose<layout::Node>(layout::Node::Type::Glue);
            glue->glue({.width = amount});
            return directive(arena, glue, origin, true);
        };

        // \abovecaptionskip on the side the float's content is, as LaTeX's
        // own placement leaves it: below a table's caption, which heads the
        // table, and above a figure's, which ends it.
        std::vector<syntax::Node*> parts;
        parts.push_back(arena.compose<syntax::Node>(syntax::Node::Type::Paragraph, std::string_view{}, origin));
        if (kind == "figure" || kind == "subfigure") parts.push_back(space(sub ? 4.0f : 10.0f));
        // Its line in the list of figures or of tables, and where it stands,
        // for the page that line prints.
        if (!sub && (kind == "figure" || kind == "table")) {
            const std::size_t index = context.anchors++;
            memory::Slice<syntax::Node*> shown = body;
            if (!brief.empty()) {
                shown = arena.allocate<syntax::Node*>(1);
                shown[0] = arena.compose<syntax::Node>(syntax::Node::Type::Text, arena.copy(brief), origin);
            }
            context.entries.push_back({kind == "figure" ? "lof" : "lot", 1, number, shown, index});
            parts.push_back(order({.command = Command::Anchor, .index = index}));
        }
        parts.push_back(order({.command = Command::Save}));
        parts.push_back(order({.command = Command::Align,
                               .justification = ruled || !single ? layout::Node::Justification::Full
                                                                  : layout::Node::Justification::Center}));
        parts.push_back(order({.command = Command::Margin, .width = 0.0f}));
        parts.push_back(order({.command = Command::Margin, .width = 0.0f, .trailing = true}));
        parts.push_back(order({.command = Command::Flush}));
        parts.push_back(directive(arena, head, origin));
        parts.push_back(directive(arena, gap, origin));
        parts.insert(parts.end(), body.begin(), body.end());
        parts.push_back(arena.compose<syntax::Node>(syntax::Node::Type::Paragraph, std::string_view{}, origin));
        parts.push_back(order({.command = Command::Restore}));

        if (ruled) {
            // The algorithm package's ruled style: a rule under the caption.
            parts.push_back(space(2.0f));
            auto* bar = arena.compose<layout::Node>(layout::Node::Type::Rule);
            bar->rule({.width = breadth(context), .height = 0.4f});
            parts.push_back(directive(arena, bar, origin, true));
            parts.push_back(space(2.0f));
        } else if (kind != "figure" && kind != "subfigure") {
            parts.push_back(space(sub ? 4.0f : 10.0f));
        }

        const memory::Slice<syntax::Node*> nodes = arena.allocate<syntax::Node*>(parts.size());
        std::ranges::copy(parts, nodes.begin());
        return arena.compose<syntax::Node>(syntax::Node::Type::Group, std::string_view{}, origin, nodes);
    }

    void Floats::operator()(syntax::Parser& parser, Context& context) const {
        // Every float LaTeX's classes and the algorithm package define, the
        // starred double-column forms set as their single-column ones are.
        // The packages' floats are set as the one of LaTeX's each is a form
        // of: rotating's sideways ones upright, sidecap's with the caption
        // under rather than beside, wrapfig's across the column rather than
        // with the text flowing round -- `[lines]{r}{width}`, read and let
        // go -- and acmart's teaser a figure too.
        static constexpr std::array<std::pair<std::string_view, std::string_view>, 14> kinds{{
            {"figure", "figure"}, {"figure*", "figure"}, {"table", "table"}, {"table*", "table"},
            {"algorithm", "algorithm"}, {"sidewaysfigure", "figure"}, {"sidewaystable", "table"},
            {"SCfigure", "figure"}, {"SCtable", "table"}, {"wrapfigure", "figure"}, {"wraptable", "table"},
            {"figwindow", "figure"}, {"tabwindow", "table"}, {"teaserfigure", "figure"},
        }};

        // One float's block: where it may go read on the way in, its marks
        // for the pager left either side of what it holds. A float of a new
        // kind is installed the same way as LaTeX's own.
        const auto install = [this, &context](const std::string& name, const std::string& kind, const bool ruled) {
            context.blocks.watch(
                name,
                [this, kind, name, ruled](syntax::Mouth& mouth) {
                    // Where LaTeX may place it -- `[htbp!]`, a letter each --
                    // and `tbp` when it does not say, as the classes give
                    // their figures and tables. `H` is the float package's:
                    // where written, never moved. An `h` alone is LaTeX's
                    // `ht`, as LaTeX makes it rather than lose the float.
                    using Directive = layout::Node::Directive;
                    bool here = false;
                    std::uint8_t place = 0;
                    for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                        for (const char letter : token.text) {
                            switch (letter) {
                                case 'H': here = true; break;
                                case 'h': place |= Directive::Here; break;
                                case 't': place |= Directive::Top; break;
                                case 'b': place |= Directive::Bottom; break;
                                case 'p': place |= Directive::Alone; break;
                                case '!': place |= Directive::Force; break;
                                default: break;
                            }
                        }
                    }
                    if (!(place & (Directive::Here | Directive::Top | Directive::Bottom | Directive::Alone))) {
                        place |= Directive::Top | Directive::Bottom | Directive::Alone;
                    }
                    if ((place & (Directive::Here | Directive::Top | Directive::Bottom | Directive::Alone)) ==
                        Directive::Here) {
                        place |= Directive::Top;
                    }
                    if (name.starts_with("wrap")) {
                        static_cast<void>(mouth.argument({}, 0));
                        static_cast<void>(mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0));
                        static_cast<void>(mouth.argument({}, 0));
                        here = true;
                    }
                    floats.push_back(
                        Open{.kind = std::string(kind), .ruled = ruled, .place = place, .across = name.ends_with('*')});

                    // The cursor is a stack: the opening text goes in first
                    // and the mark in front of it, so the mark is read first.
                    mouth.ingest(ruled ? "\\par\\vskip 12pt plus 2pt minus 2pt\\hrule height 0.8pt\\vskip 2pt"
                                         "\\leftskip=0pt\\rightskip=0pt\\justifying\\noindent "
                                       : "\\par\\vskip 12pt plus 2pt minus 2pt"
                                         "\\leftskip=0pt\\rightskip=0pt\\justifying\\noindent ");
                    // A starred float is set across every column, as wide as
                    // the text block: one column within it, as \\twocolumn's
                    // heading is.
                    if (name.ends_with('*')) mouth.ingest("\\@columns1 ");
                    const syntax::Symbol opening = here ? fixed : hold;
                    const syntax::Token mark{.symbol = opening, .category = syntax::Catcodes::Category::Escape,
                                             .text = mouth.lexicon.resolve(opening)};
                    mouth.stream().inject(std::span{&mark, 1});
                },
                [this](syntax::Mouth& mouth) {
                    const bool ruled = !floats.empty() && floats.back().ruled;
                    const bool across = !floats.empty() && floats.back().across;
                    if (!floats.empty()) floats.pop_back();

                    // The closing mark goes in first, so it is read after the
                    // closing text and the space below it -- and after the
                    // columns a starred float changed go back as they were.
                    const syntax::Token mark{.symbol = release, .category = syntax::Catcodes::Category::Escape,
                                             .text = mouth.lexicon.resolve(release)};
                    mouth.stream().inject(std::span{&mark, 1});
                    if (across) mouth.ingest("\\par\\@columns0 ");
                    mouth.ingest(ruled ? "\\par\\vskip 2pt\\hrule height 0.4pt\\vskip 12pt plus 2pt minus 2pt"
                                         "\\leftskip=0pt\\rightskip=0pt\\justifying\\noindent "
                                       : "\\par\\vskip 12pt plus 2pt minus 2pt"
                                         "\\leftskip=0pt\\rightskip=0pt\\justifying\\noindent ");
                });
        };
        for (const auto& [name, kind] : kinds) install(std::string(name), std::string(kind), kind == "algorithm");

        // A float of the document's own kind -- the float package's
        // \\newfloat, newfloat's \\DeclareFloatingEnvironment -- numbered by
        // the counter of its name and captioned by `\\<name>name`, which the
        // package defines first; `\\@newfloat*` sets it between rules, as the
        // float package's ruled style does.
        parser.mouth.bind("\\@newfloat", [install](syntax::Mouth& mouth) {
            const bool ruled = mouth.lookahead().is('*');
            if (ruled) mouth.read();
            const std::string name = syntax::Argument::text(mouth);
            if (!name.empty()) install(name, name, ruled);
        });

        // subcaption's blocks, which the box module sets as minipages: here
        // only what a caption inside one numbers by.
        for (const std::string_view kind : {"subfigure", "subtable"}) {
            context.blocks.watch(
                kind,
                [this, kind](syntax::Mouth&) { floats.push_back(Open{.kind = std::string(kind)}); },
                [this](syntax::Mouth&) {
                    if (!floats.empty()) floats.pop_back();
                });
        }

        // The marks around a float's blocks, which the pager keeps together.
        for (const auto& [symbol, command, stays] :
             {std::tuple{hold, layout::Node::Directive::Command::Hold, false},
              std::tuple{fixed, layout::Node::Directive::Command::Hold, true},
              std::tuple{release, layout::Node::Directive::Command::Release, false}}) {
            parser.bind(symbol, [this, command, stays](syntax::Parser& parser) -> syntax::Node* {
                memory::Arena& arena = parser.arena;
                auto* mark = arena.compose<layout::Node>(layout::Node::Type::Directive);
                layout::Node::Directive order{.command = command, .fixed = stays};
                // A Hold says where its float may go, and what kind it is --
                // figures, tables and algorithms each keep their own order.
                if (command == layout::Node::Directive::Command::Hold && !floats.empty()) {
                    const Open& current = floats.back();
                    order.place = current.place;
                    order.across = current.across;
                    order.index = current.kind == "figure" ? 0 : current.kind == "table" ? 1 : 2;
                }
                mark->directive(order);
                return directive(arena, mark, parser.mouth.lookahead().location);
            });
        }

        // A barrier no waiting float passes: every float still waiting is
        // set before what follows it, on pages of their own. placeins's
        // \\FloatBarrier is this.
        parser.bind("\\@floatbarrier", [](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena;
            auto* mark = arena.compose<layout::Node>(layout::Node::Type::Directive);
            mark->directive({.command = layout::Node::Directive::Command::Barrier});
            return directive(arena, mark, parser.mouth.lookahead().location, true);
        });

        parser.bind("\\caption", [this, &context](syntax::Parser& parser) -> syntax::Node* {
            const Open current = floats.empty() ? Open{.kind = "figure"} : floats.back();
            return caption(parser, context, current.kind, current.ruled);
        });

        // The caption package's: a caption of a kind named outright, wherever
        // it stands.
        parser.bind("\\captionof", [this, &context](syntax::Parser& parser) -> syntax::Node* {
            const std::string kind = syntax::Argument::text(parser.mouth);
            return caption(parser, context, kind.empty() ? "figure" : kind, false);
        });

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound float primitives");
    }

}
