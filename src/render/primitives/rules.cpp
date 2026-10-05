/// @file
/// @brief Rule primitives: `\\rule`, `\\hrule`, `\\vrule` and `\\underline`.
#include "render/primitives/rules.hpp"
#include "layout/line.hpp"
#include "logger.hpp"

#include "syntax/argument.hpp"
#include "syntax/number.hpp"
#include "syntax/semantics/scope.hpp"

#include <span>
#include <utility>
#include <vector>

namespace render::primitives {

    /// The thickness TeX gives a rule that is not measured, in points.
    static constexpr float hairline = 0.4f;

    /// @brief What a rule is drawn in: the color the text around it is set
    ///        in -- `\\textcolor{blue}{\\rule{1em}{1pt}}` -- black where none
    ///        was chosen.
    [[nodiscard]] static layout::Node::Color ink(const Context& context) noexcept {
        const layout::Node::Color* color = context.selection.color();
        return color ? *color : layout::Node::Color{};
    }

    Rules::Rules(syntax::Lexicon& lexicon) noexcept {
        lexicon.intern("\\rule");
        lexicon.intern("\\underline");
    }

    void Rules::operator()(syntax::Parser& parser, Context& context) const {
        parser.bind("\\rule", [&context](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            memory::Arena& arena = parser.arena;
            const memory::Location origin = mouth.lookahead().location;

            // `\\rule[0.5ex]{...}{...}`: how far above the baseline the rule
            // stands, a dimension in brackets read as the groups are below.
            float raise = 0.0f;
            if (mouth.lookahead().is('[')) {
                mouth.read();
                if (const auto scanned = syntax::Number::dimension(mouth, context.registers)) {
                    raise = static_cast<float>(*scanned) / static_cast<float>(syntax::Number::scale);
                }
                while (!mouth.lookahead().empty() && !mouth.lookahead().is(']')) mouth.read();
                mouth.read();
            }

            // Two brace groups, each holding one dimension. The braces are
            // read and dropped rather than parsed, because what is inside them
            // is a number and not a document.
            float measured[2]{0.0f, 0.0f};
            for (float& value : measured) {
                syntax::Token open = mouth.read();
                if (!open.is(syntax::Catcodes::Category::Group, '{') && !open.empty()) {
                    mouth.stream().inject(std::span{&open, 1});
                }

                if (const auto scanned = syntax::Number::dimension(mouth, context.registers)) {
                    value = static_cast<float>(*scanned) / static_cast<float>(syntax::Number::scale);
                }

                syntax::Token close = mouth.read();
                if (!close.is(syntax::Catcodes::Category::Group, '}') && !close.empty()) {
                    mouth.stream().inject(std::span{&close, 1});
                }
            }

            auto* node = arena.compose<layout::Node>();
            // Raised, the rule's depth is the height it stands off the
            // baseline, below nothing: it reaches from there to its top.
            node->rule({.width = measured[0], .height = measured[1] + raise, .depth = -raise, .color = ink(context)});
            return directive(arena, node, origin);
        });

        // The two unmeasured rules. Neither states a length, so each runs the
        // full way across whatever it sits in: one the width of the column,
        // the other the height of the line. A rule with no length at all would
        // draw nothing, which is the one thing a rule must not do.
        // TeX's own keywords, in any order and each as often as it likes:
        // `\\hrule height 0.8pt`, `\\vrule width 0pt height 8pt depth 3pt`.
        const auto keywords = [&context](syntax::Mouth& mouth, layout::Node::Rule& shape) {
            for (bool reading = true; reading;) {
                reading = false;
                for (const auto& [word, field] : {std::pair{"height", &layout::Node::Rule::height},
                                                  std::pair{"depth", &layout::Node::Rule::depth},
                                                  std::pair{"width", &layout::Node::Rule::width}}) {
                    if (!syntax::Argument::keyword(mouth, word)) continue;
                    if (const auto scanned = syntax::Number::dimension(mouth, context.registers)) {
                        shape.*field = static_cast<float>(*scanned) / static_cast<float>(syntax::Number::scale);
                    }
                    reading = true;
                }
            }
        };

        parser.bind("\\hrule", [&context, keywords](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena;
            const memory::Location origin = parser.mouth.lookahead().location;

            layout::Node::Rule shape{.width = breadth(context), .height = hairline, .color = ink(context)};
            keywords(parser.mouth, shape);

            auto* node = arena.compose<layout::Node>();
            node->rule(shape);
            return directive(arena, node, origin, true);
        });

        parser.bind("\\vrule", [&context, keywords](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena;
            const memory::Location origin = parser.mouth.lookahead().location;

            // Descent comes back negative from the font and a depth is a
            // distance, so it is negated here.
            const typography::Font* font = context.selection.text();
            const typography::Font::Metric line =
                font ? font->metrics() : typography::Font::Metric{};
            layout::Node::Rule shape{.width = hairline, .height = line.ascent, .depth = -line.descent, .color = ink(context)};
            keywords(parser.mouth, shape);

            auto* node = arena.compose<layout::Node>();
            node->rule(shape);
            return directive(arena, node, origin);
        });

        // The text, then a rule the width of it a little below the
        // baseline -- the same stacked-column shape a math-mode `\\underline`
        // is built from, with a fixed drop in place of a MATH table constant
        // since a text face carries none.
        parser.bind("\\underline", [&context](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            memory::Arena& arena = parser.arena;
            const memory::Location origin = mouth.lookahead().location;

            syntax::Token open = mouth.read();
            if (!open.is(syntax::Catcodes::Category::Group, '{')) {
                if (!open.empty()) mouth.stream().inject(std::span{&open, 1});
                return directive(arena, nullptr, origin);
            }

            mouth.push(syntax::semantics::Scope::Type::Group);
            const memory::Slice<syntax::Node*> content = parser.parse('}');
            mouth.pop(syntax::semantics::Scope::Type::Group);
            stamp(content, context);

            std::vector<layout::Node*> gathered;
            for (const syntax::Node* child : content) compose(gathered, child, context);
            if (gathered.empty()) return directive(arena, nullptr, origin);

            const memory::Slice<layout::Node*> row = arena.allocate<layout::Node*>(gathered.size());
            std::ranges::copy(gathered, row.begin());
            layout::Node* body = layout::Line::horizontal(arena, row, 0.0f);

            const typography::Font* font = context.selection.text();
            const float drop = font ? font->size() * 0.08f : 2.0f;

            auto* bar = arena.compose<layout::Node>(layout::Node::Type::Rule);
            bar->rule({.width = body->box().width, .height = hairline});

            auto* gap = arena.compose<layout::Node>(layout::Node::Type::Kern);
            gap->kern({.width = drop});

            const memory::Slice<layout::Node*> column = arena.allocate<layout::Node*>(3);
            column[0] = body;
            column[1] = gap;
            column[2] = bar;

            layout::Node* stacked = layout::Line::vertical(arena, column, 0.0f, layout::Line::Anchor::Middle);
            layout::Node::Box shape = stacked->box();
            shape.shift = -body->box().height;   // a column hangs from its top; raise to keep the baseline
            stacked->box(shape);

            return directive(arena, stacked, origin);
        });

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound rule primitives");
    }

}
