/// @file
/// @brief Footnote implementation: a raised mark in the line, and the note it
///        carries to the foot of its page.
///
/// A note is laid out the moment it is read, as its own layout::Paragraph --
/// the same class the document breaks its own paragraphs with -- so a long
/// note wraps exactly as a paragraph of body text would, and what travels
/// with the call is only the finished box.
#include "render/primitives/footnotes.hpp"
#include "render/primitives/counters.hpp"
#include "render/primitives/styles.hpp"
#include "syntax/semantics/scope.hpp"
#include "layout/line.hpp"
#include "layout/paragraph.hpp"
#include "logger.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <span>
#include <string_view>
#include <vector>

namespace render::primitives {

    Footnotes::Footnotes(syntax::Lexicon& lexicon) noexcept {
        lexicon.intern("\\footnote");
        lexicon.intern("\\footnotemark");
        lexicon.intern("\\footnotetext");
        lexicon.intern("\\thanks");
    }

    layout::Node* Footnotes::mark(const Context& context, memory::Arena& arena, const std::string& text,
                                  const float size) {
        // LaTeX's \@makefnmark: upright and medium whatever the text around
        // it, at the script size of the text it sits in, raised.
        const float body = context.document.configuration().size;
        const float small = Styles::measure(body, Styles::Size::Script) * size / body;
        const typography::Font* font = Styles::resolve(context, Styles::Cut::Normal, small);
        if (!font) return nullptr;

        const typography::Font* fonts[] = {font};
        const memory::Slice<layout::Node*> shaped =
            context.shaper.shape(memory::Slice{fonts, 1uz}, arena.copy(text), {});
        layout::Node* box = layout::Line::horizontal(arena, shaped, 0.0f);
        layout::Node::Box lifted = box->box();
        lifted.shift = -size * raise;
        box->box(lifted);
        return box;
    }

    layout::Node* Footnotes::note(syntax::Parser& parser, Context& context, const std::string& label) const {
        syntax::Mouth& mouth = parser.mouth();
        memory::Arena& arena = parser.arena();
        const memory::Location origin = mouth.lookahead().location;

        syntax::Token open = mouth.read();
        while (open.category == syntax::CatCodes::Category::Space) open = mouth.read();
        if (!open.is(syntax::CatCodes::Category::Group, '{')) {
            if (!open.empty()) mouth.stream().inject(std::span{&open, 1});
            tracebacks_.emplace_back(syntax::Traceback::Type::Group, origin, "A footnote needs a brace group");
            return nullptr;
        }

        // Read in the foot's own face -- \footnotesize, upright -- so a style
        // inside the note changes from that, as it does in LaTeX.
        const layout::Document::Configuration& page = context.document.configuration();
        const float size = Styles::measure(page.size, Styles::Size::Footnote);
        const typography::Font* restore = context.selection.text();
        const typography::Font* font = Styles::resolve(context, Styles::Cut::Normal, size);
        if (!font) return nullptr;
        context.selection.text(font);

        mouth.push(syntax::semantics::Scope::Type::Group);
        const memory::Slice<syntax::Node*> body = parser.parse('}');
        mouth.pop(syntax::semantics::Scope::Type::Group);
        stamp(body, context);

        // \@makefntext: the mark raised and right-aligned in a box 1.8 ems
        // wide, then the text.
        std::vector<layout::Node*> content;
        if (layout::Node* raised = mark(context, arena, label, size)) {
            auto* before = arena.compose<layout::Node>(layout::Node::Type::Kern);
            before->kern({.width = std::max(size * 1.8f - raised->box().width, 0.0f)});
            content.push_back(before);
            content.push_back(raised);
        }
        for (const syntax::Node* child : body) gather(content, child, context);
        context.selection.text(restore);

        const memory::Slice<layout::Node*> pieces = arena.allocate<layout::Node*>(content.size());
        std::ranges::copy(content, pieces.begin());

        // The foot's own baseline, as LaTeX's size tables give it: 9.5
        // points under an 8-point note.
        layout::Paragraph set(arena, pieces, layout::Node::Justification::Full, 0.0f);
        set.layout(arena, page.width - page.left - page.right, size * 1.1875f);
        return set.node();
    }

    syntax::Node* Footnotes::call(memory::Arena& arena, layout::Node* raised, layout::Node* set,
                                  const memory::Location origin) {
        const memory::Slice<syntax::Node*> parts = arena.allocate<syntax::Node*>(2);
        std::size_t filled = 0;
        if (raised) parts[filled++] = directive(arena, raised, origin);
        if (set) {
            auto* carried = arena.compose<layout::Node>(layout::Node::Type::Directive);
            carried->directive({.command = layout::Node::Directive::Command::Note, .note = set});
            parts[filled++] = directive(arena, carried, origin);
        }
        return arena.compose<syntax::Node>(syntax::Node::Type::Group, std::string_view{}, origin,
                                           memory::Slice{parts.data, filled});
    }

    void Footnotes::operator()(syntax::Parser& parser, Context& context) const {
        // The number a call gives out: the next of the footnote counter's, or
        // the one it names, `\footnote[7]{...}`.
        const auto number = [&context](syntax::Mouth& mouth) -> std::string {
            const std::vector<syntax::Token> given = mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0);
            if (!context.counters) return {};
            if (given.empty()) {
                context.counters->step("footnote");
            } else {
                std::string digits;
                for (const syntax::Token& token : given) digits += token.text;
                context.counters->set("footnote", std::atoi(digits.c_str()));
            }
            return Counters::print(mouth, "footnote");
        };

        const auto current = [&context]() {
            return context.selection.text() ? context.selection.text()->size() : context.document.configuration().size;
        };

        parser.bind("\\footnote", [this, &context, number, current](syntax::Parser& parser) -> syntax::Node* {
            const memory::Location origin = parser.mouth().lookahead().location;
            if (!context.selection.text()) {
                tracebacks_.emplace_back(syntax::Traceback::Type::Primitive, origin,
                                         "\\footnote needs a text face to be set in");
                return nullptr;
            }
            const std::string label = number(parser.mouth());
            layout::Node* raised = mark(context, parser.arena(), label, current());
            return call(parser.arena(), raised, note(parser, context, label), origin);
        });

        parser.bind("\\footnotemark", [&context, number, current](syntax::Parser& parser) -> syntax::Node* {
            const memory::Location origin = parser.mouth().lookahead().location;
            const std::string label = number(parser.mouth());
            return call(parser.arena(), mark(context, parser.arena(), label, current()), nullptr, origin);
        });

        parser.bind("\\footnotetext", [this, &context](syntax::Parser& parser) -> syntax::Node* {
            const memory::Location origin = parser.mouth().lookahead().location;
            std::string label;
            const std::vector<syntax::Token> given =
                parser.mouth().argument(syntax::Mouth::Parameter{.optional = true}, 0);
            for (const syntax::Token& token : given) label += token.text;
            if (label.empty() && context.counters) label = Counters::print(parser.mouth(), "footnote");
            return call(parser.arena(), nullptr, note(parser, context, label), origin);
        });

        // A title's notes: marked with LaTeX's footnote symbols in turn,
        // counted apart from the numbered notes the text calls.
        parser.bind("\\thanks", [this, &context, current](syntax::Parser& parser) -> syntax::Node* {
            static constexpr std::array<std::string_view, 9> symbols{
                "*", "†", "‡", "§", "¶", "‖", "**", "††", "‡‡",
            };
            const memory::Location origin = parser.mouth().lookahead().location;
            const std::string label(symbols[static_cast<std::size_t>(thanked++) % symbols.size()]);
            layout::Node* raised = mark(context, parser.arena(), label, current());
            return call(parser.arena(), raised, note(parser, context, label), origin);
        });

        // \marginpar{...}: a note in the right margin, \marginparwidth wide,
        // level with the line it is written in; the bracketed form a
        // two-sided document would set in the left margin is read and let
        // go. marginnote's \marginnote is the same note.
        for (const std::string_view name : {"\\marginpar", "\\marginnote"}) {
            parser.bind(name, [&context](syntax::Parser& parser) -> syntax::Node* {
                syntax::Mouth& mouth = parser.mouth();
                memory::Arena& arena = parser.arena();
                const memory::Location origin = mouth.lookahead().location;
                static_cast<void>(mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0));

                syntax::Token open = mouth.read();
                while (open.category == syntax::CatCodes::Category::Space) open = mouth.read();
                if (!open.is(syntax::CatCodes::Category::Group, '{')) {
                    if (!open.empty()) mouth.stream().inject(std::span{&open, 1});
                    return nullptr;
                }
                const Selection kept = context.selection;
                mouth.push(syntax::semantics::Scope::Type::Group);
                const memory::Slice<syntax::Node*> body = parser.parse('}');
                mouth.pop(syntax::semantics::Scope::Type::Group);
                context.selection = kept;

                std::vector<layout::Node*> content;
                for (const syntax::Node* child : body) gather(content, child, context);
                if (content.empty()) return nullptr;
                const memory::Slice<layout::Node*> pieces = arena.allocate<layout::Node*>(content.size());
                std::ranges::copy(content, pieces.begin());

                // As wide as \marginparwidth, or as the margin has room for.
                const layout::Document::Configuration& page = context.document.configuration();
                const auto length = [&context, &mouth](const std::string_view called, const float fallback) {
                    const auto slot = context.registers.target(mouth.lexicon().intern(called));
                    return slot ? static_cast<float>(context.registers.get(slot->type, slot->slot)) / 65536.0f : fallback;
                };
                const float separation = length("\\marginparsep", 11.0f);
                const float width = std::min(length("\\marginparwidth", 57.0f), page.right - separation - 6.0f);
                if (width <= 0.0f) return nullptr;
                layout::Paragraph set(arena, pieces, layout::Node::Justification::Left, 0.0f);
                set.layout(arena, width, page.leading);
                if (!set.node()) return nullptr;

                auto* aside = arena.compose<layout::Node>(layout::Node::Type::Directive);
                aside->directive({.command = layout::Node::Directive::Command::Aside, .width = separation,
                                  .note = set.node()});
                return directive(arena, aside, origin);
            });
        }

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound footnote primitives");
    }

}
