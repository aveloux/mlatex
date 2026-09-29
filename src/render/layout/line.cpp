/// @file
/// @brief Line implementation: measuring a run of nodes as one box.
///
/// advance() and extent() are the two rules everything else here follows, and
/// the drawing code follows the same two. Keeping them in one place is what
/// stops a box from claiming one size and occupying another.
#include "layout/line.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include <algorithm>
#include <array>

namespace render::layout {

    float Line::advance(const Node* node) noexcept {
        if (!node) return 0.0f;
        switch (node->type()) {
            case Node::Type::Box:    return node->box().width;
            case Node::Type::Glyph:  return node->glyph().width;
            case Node::Type::Rule:   return node->rule().width;
            case Node::Type::Bitmap: return node->bitmap().width;
            case Node::Type::Kern:   return node->kern().width;
            case Node::Type::Glue:   return node->glue().width;
            // A page number is only known once the page is, so it takes the
            // room it was given when it was read.
            case Node::Type::Directive:
                return node->directive().command == Node::Directive::Command::Number ? node->directive().width : 0.0f;
            default:                 return 0.0f;   // penalties and marks take no room
        }
    }

    float Line::extent(const Node* node) noexcept {
        if (!node) return 0.0f;
        switch (node->type()) {
            case Node::Type::Box:    return node->box().height + node->box().depth;
            case Node::Type::Glyph:  return node->glyph().height + node->glyph().depth;
            case Node::Type::Rule:   return node->rule().height + node->rule().depth;
            case Node::Type::Bitmap: return node->bitmap().height;   // depth is always zero
            // A kern or a glue between two lines is vertical space, and its
            // one measurement is its width whichever way it is being read.
            case Node::Type::Kern:   return node->kern().width;
            case Node::Type::Glue:   return node->glue().width;
            default:                 return 0.0f;
        }
    }

    Node* Line::horizontal(
        memory::Arena& arena,
        const memory::Slice<Node*> nodes,
        const float target
    ) noexcept {
        auto* box = arena.compose<Node>(Node::Type::Box);

        float natural = 0.0f;
        float height = 0.0f;
        float depth = 0.0f;

        // One accumulator per order of infinity. A finite stretch loses to any
        // infinite one, and each order of infinity to the next, which is how
        // `\\hfil` yields to `\\hfill` in the same line.
        std::array<float, 4> stretch{};
        std::array<float, 4> shrink{};

        for (const Node* node : nodes) {
            if (!node) continue;

            natural += advance(node);

            switch (node->type()) {
                case Node::Type::Box: {
                    // A shifted box carries its ink with it: pushed down the
                    // page, it needs less room above the line and more below.
                    const auto& inner = node->box();
                    height = std::max(height, inner.height - inner.shift);
                    depth = std::max(depth, inner.depth + inner.shift);
                    break;
                }
                case Node::Type::Glyph:
                    height = std::max(height, node->glyph().height);
                    depth = std::max(depth, node->glyph().depth);
                    break;
                case Node::Type::Rule:
                    height = std::max(height, node->rule().height);
                    depth = std::max(depth, node->rule().depth);
                    break;
                case Node::Type::Bitmap:
                    height = std::max(height, node->bitmap().height);
                    break;
                case Node::Type::Glue: {
                    const auto& glue = node->glue();
                    stretch[static_cast<std::size_t>(glue.expand)] += glue.stretch;
                    shrink[static_cast<std::size_t>(glue.limit)] += glue.shrink;
                    break;
                }
                default:
                    break;
            }
        }

        float ratio = 0.0f;
        Node::Sign sign = Node::Sign::None;
        std::size_t order = 0;

        if (target > 0.0f && natural != target) {
            const bool loose = natural < target;
            const std::array<float, 4>& flex = loose ? stretch : shrink;

            // Highest order present wins outright; the lower ones then take
            // none of the difference at all, which is why the order is kept
            // with the ratio: a centred line's word spaces stay their own
            // width while the fil glue at its edges takes up the rest.
            for (std::size_t step = 3; step > 0; --step) {
                if (flex[step] > 0.0f) {
                    order = step;
                    break;
                }
            }

            if (flex[order] > 0.0f) {
                ratio = std::abs(target - natural) / flex[order];
                sign = loose ? Node::Sign::Stretching : Node::Sign::Shrinking;
                // Glue shrinks only as far as it said it could; past that the
                // line is overfull and is left overfull rather than collapsed.
                // Infinite shrink has no such bound: `\\hss` gives whatever it
                // is asked for, which is how `\\makebox[0pt][l]` overlaps.
                if (!loose && order == 0) ratio = std::min(ratio, 1.0f);
            }
        }

        box->box({
            .width = target > 0.0f ? target : natural,
            .height = height,
            .depth = depth,
            .ratio = ratio,
            .sign = sign,
            .order = static_cast<Node::Order>(order),
            .alignment = Node::Alignment::Horizontal,
            .list = nodes
        });
        return box;
    }

    Node* Line::vertical(
        memory::Arena& arena,
        const memory::Slice<Node*> nodes,
        const float skip,
        const Anchor anchor
    ) noexcept {
        auto* box = arena.compose<Node>(Node::Type::Box);

        // Room for one leading glue between each pair, when leading is asked for.
        const std::size_t count = nodes.size();
        const memory::Slice<Node*> column =
            arena.allocate<Node*>(skip > 0.0f && count > 1 ? count * 2 - 1 : count);

        float width = 0.0f;
        float height = 0.0f;
        std::size_t filled = 0;

        for (std::size_t index = 0; index < count; ++index) {
            Node* child = nodes[index];
            if (!child) continue;

            if (skip > 0.0f && filled > 0) {
                auto* glue = arena.compose<Node>(Node::Type::Glue);
                glue->glue({.width = skip});
                column[filled++] = glue;
                height += skip;
            }

            width = std::max(width, advance(child));
            height += extent(child);
            column[filled++] = child;
        }

        // Centring is a per-box offset rather than a property of the column,
        // so a fraction's numerator lines up over its bar while a page of
        // text stays flush left.
        if (anchor == Anchor::Middle) {
            for (std::size_t index = 0; index < filled; ++index) {
                Node* child = column[index];
                if (!child || child->type() != Node::Type::Box) continue;

                Node::Box shape = child->box();
                shape.offset = (width - shape.width) * 0.5f;
                child->box(shape);
            }
        }

        // All depth and no height: the reference point sits on the top edge,
        // so the whole run hangs below it. That is what makes a column drop
        // into a line without a rule of its own -- the one rule everything
        // follows is "draw at the reference point", and a column's is its top.
        box->box({
            .width = width,
            .height = 0.0f,
            .depth = height,
            .alignment = Node::Alignment::Vertical,
            .list = memory::Slice{column.data, filled}
        });
        return box;
    }

    void Line::reorder(memory::Arena& arena, Node* line, const bool reversed) {
        // Which way each character runs: 1 for a letter read left to right,
        // 2 for one read right to left -- Hebrew, Arabic, Syriac, Thaana,
        // N'Ko and their presentation forms -- 3 for a digit, and 0 for
        // anything else: a space, a mark of punctuation, a symbol.
        const auto sense = [](const std::uint32_t code) -> int {
            if ((code >= 0x0590 && code <= 0x08FF) || (code >= 0xFB1D && code <= 0xFDFF) ||
                (code >= 0xFE70 && code <= 0xFEFF)) {
                return (code >= 0x0660 && code <= 0x0669) || (code >= 0x06F0 && code <= 0x06F9) ? 3 : 2;
            }
            if (code >= '0' && code <= '9') return 3;
            if ((code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z') ||
                (code >= 0xC0 && code < 0x2000 && code != 0xD7 && code != 0xF7) || code >= 0x3000) {
                return 1;
            }
            return 0;
        };

        // What a node's text runs as: its first letter's way, or a number's
        // when it holds digits and no letter, or 0. A box inside the line --
        // a formula, a word in another face -- the same over what it holds.
        const auto run = [&sense](this const auto& self, const Node* node) -> int {
            if (!node) return 0;
            if (node->type() == Node::Type::Glyph) return sense(node->glyph().point);
            if (node->type() != Node::Type::Box) return 0;
            int found = 0;
            for (const Node* child : node->box().list) {
                const int way = self(child);
                if (way == 1 || way == 2) return way;
                if (way == 3) found = 3;
            }
            return found;
        };

        // Only a line with something right to left in it, or in a paragraph
        // that reads so, has anything to put in another order.
        if (!line || line->type() != Node::Type::Box || line->box().alignment != Node::Alignment::Horizontal) return;
        const memory::Slice<Node*> list = line->box().list;
        bool mixed = reversed;
        for (std::size_t index = 0; !mixed && index < list.count; ++index) mixed = run(list[index]) == 2;
        if (!mixed) return;

        // The line cut into pieces: a word -- the glyphs and the breaks
        // between them that no space parts -- and each space, kern or
        // box on its own. Each piece keeps its nodes' order.
        std::vector<std::pair<std::size_t, std::size_t>> pieces;
        std::vector<int> ways;
        for (std::size_t at = 0; at < list.count;) {
            std::size_t end = at + 1;
            const auto inside = [&](const Node* node) {
                return node && (node->type() == Node::Type::Glyph || node->type() == Node::Type::Penalty);
            };
            if (inside(list[at])) {
                while (end < list.count && inside(list[end])) ++end;
            }
            int way = 0;
            for (std::size_t step = at; step < end; ++step) {
                const int found = run(list[step]);
                if (found == 1 || found == 2) {
                    way = found;
                    break;
                }
                if (found == 3) way = 3;
            }
            pieces.emplace_back(at, end);
            ways.push_back(way);
            at = end;
        }

        // Each piece's level, as the algorithm resolves it, a word at
        // a time. A number takes the way of the last letter before it,
        // or the paragraph's; either way it keeps its own order, a
        // level up from right-to-left text. What runs neither way
        // takes the way of what is either side of it when both agree,
        // and the paragraph's when they do not.
        const int base = reversed ? 1 : 0;
        std::vector<int> levels(pieces.size(), base);
        std::vector<int> strong(pieces.size(), 0);   // 1 left to right, 2 right to left, 0 none
        int last = reversed ? 2 : 1;
        for (std::size_t index = 0; index < pieces.size(); ++index) {
            if (ways[index] == 1 || ways[index] == 2) last = ways[index];
            if (ways[index] == 1) strong[index] = 1;
            if (ways[index] == 2) strong[index] = 2;
            if (ways[index] == 3) strong[index] = last == 2 ? 2 : 1;

            if (ways[index] == 2) levels[index] = 1;
            else if (ways[index] == 1) levels[index] = reversed ? 2 : 0;
            else if (ways[index] == 3) levels[index] = reversed || last == 2 ? 2 : 0;
        }
        for (std::size_t index = 0; index < pieces.size(); ++index) {
            if (strong[index] != 0) continue;
            std::size_t end = index;
            while (end < pieces.size() && strong[end] == 0) ++end;
            const int before = index > 0 ? strong[index - 1] : (reversed ? 2 : 1);
            const int after = end < pieces.size() ? strong[end] : (reversed ? 2 : 1);
            const int level = before != after ? base : before == 2 ? 1 : reversed ? 2 : 0;
            for (std::size_t step = index; step < end; ++step) levels[step] = level;
            index = end - 1;
        }

        // Reversed from the highest level down to the lowest odd one:
        // every run of pieces at that level or above, turned round.
        std::vector<std::size_t> order(pieces.size());
        for (std::size_t index = 0; index < order.size(); ++index) order[index] = index;
        const int highest = *std::ranges::max_element(levels);
        for (int level = highest; level >= 1; --level) {
            for (std::size_t index = 0; index < order.size();) {
                if (levels[order[index]] < level) {
                    ++index;
                    continue;
                }
                std::size_t end = index;
                while (end < order.size() && levels[order[end]] >= level) ++end;
                std::reverse(order.begin() + static_cast<std::ptrdiff_t>(index),
                             order.begin() + static_cast<std::ptrdiff_t>(end));
                index = end;
            }
        }

        const memory::Slice<Node*> drawn = arena.allocate<Node*>(list.count);
        std::size_t filled = 0;
        for (const std::size_t index : order) {
            for (std::size_t step = pieces[index].first; step < pieces[index].second; ++step) {
                drawn[filled++] = list[step];
            }
        }
        Node::Box shape = line->box();
        shape.list = drawn;
        line->box(shape);
    }

}
