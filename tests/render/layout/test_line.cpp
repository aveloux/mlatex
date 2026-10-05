#include "latex.hpp"
#include "layout/line.hpp"
#include "memory/arena.hpp"
#include "typography/collection.hpp"
#include "typography/font.hpp"
#include "typography/registry.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <vector>

// Lines and columns: nodes packed across into a box set to a width, the
// glue in it stretched or shrunk to reach it -- the highest order of
// infinity present taking all of the difference -- and nodes stacked down
// into a column that hangs from its top edge.

namespace layout = render::layout;

/// @brief A glyph-sized box of so much ink, for measuring.
static layout::Node* block(memory::Arena& arena, const float width, const float height, const float depth = 0.0f) {
    auto* node = arena.compose<layout::Node>(layout::Node::Type::Box);
    node->box({.width = width, .height = height, .depth = depth});
    return node;
}

/// @brief Glue of a width, a stretch and a shrink, at an order.
static layout::Node* glue(memory::Arena& arena, const float width, const float stretch, const float shrink,
                          const layout::Node::Order order = layout::Node::Order::Normal) {
    auto* node = arena.compose<layout::Node>(layout::Node::Type::Glue);
    node->glue({.width = width, .stretch = stretch, .shrink = shrink, .expand = order, .limit = order});
    return node;
}

/// @brief A slice of nodes, in the arena.
static memory::Slice<layout::Node*> list(memory::Arena& arena, const std::initializer_list<layout::Node*> nodes) {
    const memory::Slice<layout::Node*> slice = arena.allocate<layout::Node*>(nodes.size());
    std::size_t index = 0;
    for (layout::Node* node : nodes) slice[index++] = node;
    return slice;
}

int main() {
    memory::Arena arena(1u << 20);
    using Line = layout::Line;

    // --- Across --------------------------------------------------------------------
    {
        const layout::Node* natural =
            Line::horizontal(arena, list(arena, {block(arena, 10, 7, 2), glue(arena, 5, 2, 1), block(arena, 10, 8)}),
                             0);
        assert((natural->box().width == 25.0f) && "a line's natural width is its nodes' advances");
        assert((natural->box().height == 8.0f && natural->box().depth == 2.0f) &&
               "and it is as tall and deep as its tallest");
        assert((natural->box().sign == layout::Node::Sign::None) && "set at its natural width, nothing flexes");
    }
    {
        const layout::Node* wide =
            Line::horizontal(arena, list(arena, {block(arena, 10, 7), glue(arena, 5, 2, 1), block(arena, 10, 7)}), 29);
        assert((wide->box().width == 29.0f && wide->box().sign == layout::Node::Sign::Stretching &&
               std::abs(wide->box().ratio - 2.0f) < 0.001f) &&
               "set wider, its glue stretches by what it lacks over the stretch there is");
        const layout::Node* narrow =
            Line::horizontal(arena, list(arena, {block(arena, 10, 7), glue(arena, 5, 2, 1), block(arena, 10, 7)}), 20);
        assert((narrow->box().sign == layout::Node::Sign::Shrinking && narrow->box().ratio == 1.0f) &&
               "set narrower, it shrinks no further than it may");
    }
    {
        const layout::Node* filled = Line::horizontal(
            arena,
            list(arena, {block(arena, 10, 7), glue(arena, 5, 2, 1), glue(arena, 0, 1, 0, layout::Node::Order::Fil)}),
            100);
        assert((filled->box().order == layout::Node::Order::Fil && std::abs(filled->box().ratio - 85.0f) < 0.001f) &&
               "a fil takes all of the difference, the finite glue none");
        const layout::Node* overlap = Line::horizontal(
            arena, list(arena, {block(arena, 30, 7), glue(arena, 0, 0, 1, layout::Node::Order::Fil)}), 10);
        assert((overlap->box().width == 10.0f && std::abs(overlap->box().ratio - 20.0f) < 0.001f) &&
               "infinite shrink gives whatever it is asked for, which is how a box overlaps");
    }
    {
        layout::Node* raised = block(arena, 5, 4, 0);
        layout::Node::Box shape = raised->box();
        shape.shift = -3.0f;
        raised->box(shape);
        const layout::Node* line = Line::horizontal(arena, list(arena, {raised}), 0);
        assert((line->box().height == 7.0f) && "a box raised in a line raises the line's height with it");
    }

    // --- Down ------------------------------------------------------------------------
    {
        const layout::Node* column =
            Line::vertical(arena, list(arena, {block(arena, 30, 7, 2), block(arena, 50, 6, 3)}), 0);
        assert((column->box().height == 0.0f && column->box().depth == 18.0f) &&
               "a column hangs from its top: no height, all depth");
        assert((column->box().width == 50.0f) && "as wide as its widest");
        const layout::Node* spaced =
            Line::vertical(arena, list(arena, {block(arena, 30, 7), block(arena, 30, 7)}), 4);
        assert((spaced->box().depth == 18.0f && spaced->box().list.size() == 3) && "leading asked for goes between");
        const layout::Node* centred = Line::vertical(
            arena, list(arena, {block(arena, 10, 7), block(arena, 50, 7)}), 0, Line::Anchor::Middle);
        assert((centred->box().list[0]->box().offset == 20.0f) && "a centred column moves each box to the middle");
    }

    // --- Measures ----------------------------------------------------------------------
    {
        auto* kern = arena.compose<layout::Node>(layout::Node::Type::Kern);
        kern->kern({.width = -3.0f});
        auto* rule = arena.compose<layout::Node>(layout::Node::Type::Rule);
        rule->rule({.width = 20.0f, .height = 2.0f, .depth = 1.0f});
        assert((Line::advance(kern) == -3.0f) && "a kern advances by its width, backwards too");
        assert((Line::advance(rule) == 20.0f && Line::extent(rule) == 3.0f) &&
               "a rule by its width, and stands its height and depth");
    }

    // --- The order a line is drawn in ---------------------------------------------------
    {
        // A word of one letter, standing for any word in that letter's script.
        const auto word = [&arena](const std::uint32_t point) {
            auto* node = arena.compose<layout::Node>(layout::Node::Type::Glyph);
            node->glyph({.width = 5.0f, .point = point});
            return node;
        };
        const auto space = [&arena] { return glue(arena, 3.0f, 1.0f, 1.0f); };
        const auto points = [](const layout::Node* line) {
            std::vector<std::uint32_t> found;
            for (const layout::Node* node : line->box().list) {
                if (node->type == layout::Node::Type::Glyph) found.push_back(node->glyph().point);
            }
            return found;
        };
        constexpr std::uint32_t alef = 0x0627, beh = 0x0628, a = 'a', b = 'b', one = '1';

        layout::Node* latin = Line::horizontal(arena, list(arena, {word(a), space(), word(b)}));
        const layout::Node* const* before = latin->box().list.data;
        Line::reorder(arena, latin, false);
        assert((latin->box().list.data == before) && "a line with nothing right to left in it is left as it is");

        layout::Node* quoted = Line::horizontal(arena, list(arena, {word(a), space(), word(alef), space(), word(beh),
                                                                    space(), word(b)}));
        Line::reorder(arena, quoted, false);
        assert((points(quoted) == std::vector<std::uint32_t>{a, beh, alef, b}) &&
               "right-to-left words in a line read left to right are turned round, the rest kept");

        layout::Node* arabic = Line::horizontal(arena, list(arena, {word(alef), space(), word(beh), space(), word(a),
                                                                    space(), word(b)}));
        Line::reorder(arena, arabic, true);
        assert((points(arabic) == std::vector<std::uint32_t>{a, b, beh, alef}) &&
               "a line read right to left starts at the right, and a Latin phrase in it keeps its order");

        layout::Node* counted = Line::horizontal(arena, list(arena, {word(a), space(), word(alef), space(), word(one)}));
        Line::reorder(arena, counted, false);
        assert((points(counted) == std::vector<std::uint32_t>{a, one, alef}) &&
               "a number after right-to-left words stands to their left, as it follows them");

        // A line with nothing in it -- an empty cell of a table read right to
        // left -- has nothing to put in another order, and is left as it is.
        layout::Node* empty = Line::horizontal(arena, list(arena, {}));
        Line::reorder(arena, empty, true);
        assert((empty->box().list.empty()) && "an empty line read right to left");
    }

    // --- A word in two faces, read right to left ----------------------------------------
    {
        memory::Arena faces(1u << 24);
        render::typography::Collection collection(faces);
        render::typography::Registry registry(faces, collection);
        collection.post((latex::locate(__FILE__) / "fonts").string());
        collection.set("text", "lmroman10-regular");
        const render::typography::Font* roman = registry.get({.family = "text", .size = 10.0f});
        const render::typography::Font* naskh = registry.get({.family = "notonaskharabic-regular", .size = 10.0f});
        assert((roman && naskh) && "the text face and the Arabic one open");

        const auto letter = [&arena](const render::typography::Font* font, const std::uint32_t point) {
            auto* node = arena.compose<layout::Node>(layout::Node::Type::Glyph);
            node->glyph({.width = 5.0f, .code = font->index(point), .point = point, .font = font});
            return node;
        };
        constexpr std::uint32_t alef = 0x0627, beh = 0x0628;

        // `با.` as the shaper hands it in: the Arabic face's letters in the
        // order they are drawn, then the text face's stop. Read right to
        // left, the stop ends the sentence at its left.
        layout::Node* stopped = Line::horizontal(arena, list(arena, {letter(naskh, alef), letter(naskh, beh),
                                                                     letter(roman, '.')}));
        Line::reorder(arena, stopped, true);
        const memory::Slice<layout::Node*> ended = stopped->box().list;
        assert((ended[0]->glyph().point == '.' && ended[1]->glyph().point == alef && ended[2]->glyph().point == beh) &&
               "a stop in the text face goes to the left of the Arabic word it ends");

        // `(با)`: each bracket the text face's, so neither was mirrored by
        // the shaper; read right to left, each is drawn facing the other way.
        layout::Node* bracketed = Line::horizontal(arena, list(arena, {letter(roman, '('), letter(naskh, alef),
                                                                       letter(naskh, beh), letter(roman, ')')}));
        Line::reorder(arena, bracketed, true);
        const memory::Slice<layout::Node*> faced = bracketed->box().list;
        assert((faced[0]->glyph().point == ')' && faced[0]->glyph().code == roman->index('(') &&
                faced[3]->glyph().point == '(' && faced[3]->glyph().code == roman->index(')')) &&
               "a bracket read right to left is drawn facing the other way, and still stands for itself");

        // Read left to right, the same word in the same faces is kept.
        layout::Node* forward = Line::horizontal(arena, list(arena, {letter(roman, '('), letter(naskh, alef),
                                                                     letter(naskh, beh), letter(roman, ')')}));
        Line::reorder(arena, forward, false);
        const memory::Slice<layout::Node*> straight = forward->box().list;
        assert((straight[0]->glyph().point == '(' && straight[0]->glyph().code == roman->index('(') &&
                straight[3]->glyph().point == ')') &&
               "and in a line read left to right, the brackets stand as they were");
    }

    return 0;
}
