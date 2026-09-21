#include "layout/typesetter.hpp"
#include "layout/line.hpp"

#include <algorithm>

namespace render::layout {

    Typesetter::Typesetter(
        memory::Arena& arena,
        memory::Arena& scratch
    ) noexcept
        : arena(arena), scratch(scratch) {
        setup();
    }

    Typesetter::Typesetter(
        memory::Arena& arena,
        memory::Arena& scratch,
        const Metrics& metrics
    ) noexcept
        : arena(arena), scratch(scratch), config(metrics) {
        setup();
    }

    void Typesetter::setup() {
        bind(syntax::expression::Node::Type::Variable, [](
            const syntax::expression::Node* node,
            const typography::Font& font,
            const float target,
            const float scale,
            const Typesetter& engine
        ) -> Node* {
            const auto bounds = font.bounds(node->codepoint, scale);
            auto* glyph = engine.memory().compose<Node>(Node::Type::Glyph);
            glyph->glyph({
                .width = bounds.width,
                .height = bounds.height,
                .depth = 0.0f,
                .x = 0.0f,
                .y = 0.0f,
                .code = node->codepoint,
                .font = &font
            });
            return glyph;
        });

        bind(syntax::expression::Node::Type::Fraction, [](
            const syntax::expression::Node* node,
            const typography::Font& font,
            const float target,
            const float scale,
            const Typesetter& engine
        ) -> Node* {
            const float next = scale * engine.metrics().fraction;
            auto* top = engine.lower(node->left, font, target, next);
            auto* bottom = engine.lower(node->right, font, target, next);

            const float upper = top ? top->box().width : 0.0f;
            const float lower = bottom ? bottom->box().width : 0.0f;
            const float width = std::max(upper, lower) + (engine.metrics().padding * 2.0f * scale);

            auto* rule = engine.memory().compose<Node>(Node::Type::Rule);
            rule->rule({
                .width = width,
                .height = engine.metrics().rule * scale,
                .depth = 0.0f
            });

            auto* gap = engine.memory().compose<Node>(Node::Type::Kern);
            gap->kern({ .width = engine.metrics().padding * scale });

            std::size_t count = 1;
            if (top) count += 2;
            if (bottom) count += 2;

            auto slice = engine.memory().allocate<Node*>(count);
            std::size_t mark = 0;
            if (top) {
                slice[mark++] = top;
                slice[mark++] = gap;
            }
            slice[mark++] = rule;
            if (bottom) {
                slice[mark++] = gap;
                slice[mark++] = bottom;
            }

            return Line::vertical(engine.memory(), slice, 0.0f);
        });

        bind(syntax::expression::Node::Type::Script, [](
            const syntax::expression::Node* node,
            const typography::Font& font,
            const float target,
            const float scale,
            const Typesetter& engine
        ) -> Node* {
            auto* base = engine.lower(node->left, font, target, scale);
            const float next = scale * engine.metrics().script;
            auto* sub = engine.lower(node->subscript, font, target, next);
            auto* super = engine.lower(node->superscript, font, target, next);

            if (!sub && !super) return base;

            std::size_t count = 0;
            if (super) ++count;
            if (sub) ++count;

            auto stack = engine.memory().allocate<Node*>(count);
            std::size_t mark = 0;
            if (super) stack[mark++] = super;
            if (sub) stack[mark++] = sub;

            auto* script = Line::vertical(engine.memory(), stack, 0.0f);

            std::size_t total = 0;
            if (base) ++total;
            if (script) ++total;

            auto slice = engine.memory().allocate<Node*>(total);
            std::size_t index = 0;
            if (base) slice[index++] = base;
            if (script) slice[index++] = script;

            return Line::horizontal(engine.memory(), slice, 0.0f);
        });

        auto group = [](
            const syntax::expression::Node* node,
            const typography::Font& font,
            const float target,
            const float scale,
            const Typesetter& engine
        ) -> Node* {
            const std::size_t count = node->arguments.count;
            if (count == 0) return nullptr;

            auto slice = engine.memory().allocate<Node*>(count);
            for (std::size_t step = 0; step < count; ++step) {
                slice[step] = engine.lower(node->arguments[step], font, target, scale);
            }
            return Line::horizontal(engine.memory(), slice, 0.0f);
        };

        bind(syntax::expression::Node::Type::Sequence, group);
        bind(syntax::expression::Node::Type::Group, group);
    }

    void Typesetter::bind(const syntax::expression::Node::Type type, Lower handler) {
        handlers[type] = std::move(handler);
    }

    void Typesetter::bind(const std::string_view name, syntax::Parser::Handler handler, syntax::Parser& parser) {
        parser.bind(name, std::move(handler));
    }

    Node* Typesetter::lower(const syntax::expression::Node* node, const typography::Font& font, const float target, const float scale) const {
        if (!node) return nullptr;

        if (node->style == syntax::expression::Node::Style::Display) {
            syntax::expression::Node temp = *node;
            temp.style = syntax::expression::Node::Style::Inline;
            auto* box = lower(&temp, font, target, scale);
            if (!box) return nullptr;

            auto* left = arena.compose<Node>(Node::Type::Glue);
            left->glue({ .width = 0.0f, .stretch = 1.0f, .expand = Node::Order::Fil });

            auto* right = arena.compose<Node>(Node::Type::Glue);
            right->glue({ .width = 0.0f, .stretch = 1.0f, .expand = Node::Order::Fil });

            auto slice = arena.allocate<Node*>(3);
            slice[0] = left;
            slice[1] = box;
            slice[2] = right;

            return Line::horizontal(arena, slice, target);
        }

        const auto match = handlers.find(node->type);
        if (match != handlers.end() && match->second) {
            return match->second(node, font, target, scale, *this);
        }

        return nullptr;
    }

    Node* Typesetter::stack(memory::Slice<Node*> input) const noexcept {
        const std::size_t count = input.size();
        if (count == 0) return nullptr;

        const std::size_t total = count * 2;
        auto slice = scratch.allocate<Node*>(total);
        std::size_t mark = 0;

        float depth = 0.0f;

        for (std::size_t step = 0; step < count; ++step) {
            Node* child = input[step];
            if (!child) continue;

            if (child->type() == Node::Type::Box) {
                const auto& box = child->box();
                if (step > 0) {
                    const float gap = config.baseline - (depth + box.height);
                    auto* glue = arena.compose<Node>(Node::Type::Glue);
                    if (gap >= config.limit) {
                        glue->glue({ .width = gap, .stretch = 0.0f, .shrink = 0.0f });
                    } else {
                        glue->glue({ .width = config.skip, .stretch = 0.0f, .shrink = 0.0f });
                    }
                    slice[mark++] = glue;
                }
                depth = box.depth;
            }

            slice[mark++] = child;
        }

        auto result = arena.allocate<Node*>(mark);
        for (std::size_t step = 0; step < mark; ++step) {
            result[step] = slice[step];
        }

        return Line::vertical(arena, result, 0.0f);
    }

    memory::Slice<Pager::Page> Typesetter::compose(Document& document) const {
        document.layout();

        const memory::Slice<Document::Element*> elements = document.elements();
        const std::size_t count = elements.size();
        if (count == 0) return {};

        auto slice = scratch.allocate<Node*>(count);
        std::size_t mark = 0;

        const float width = document.configuration().width - document.configuration().left - document.configuration().right;

        for (std::size_t step = 0; step < count; ++step) {
            const Document::Element* element = elements[step];
            if (!element) continue;

            if (element->type == Document::Element::Type::Paragraph && element->paragraph) {
                if (Node* box = element->paragraph->node()) {
                    slice[mark++] = box;
                }
            } else if (element->type == Document::Element::Type::Expression && element->expression && element->font) {
                if (Node* box = lower(element->expression, *element->font, width)) {
                    slice[mark++] = box;
                }
            } else if (element->type == Document::Element::Type::Directive && element->node) {
                slice[mark++] = element->node;
            }
        }

        auto input = arena.allocate<Node*>(mark);
        for (std::size_t step = 0; step < mark; ++step) {
            input[step] = slice[step];
        }

        const Node* root = stack(input);
        if (!root) return {};

        const Pager pager(arena);
        const Pager::Context context{ .height = document.configuration().height };
        return pager.paginate(root, context);
    }

}