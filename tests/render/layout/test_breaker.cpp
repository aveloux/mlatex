#include "layout/breaker.hpp"
#include "memory/arena.hpp"

#include <cassert>
#include <cmath>
#include <vector>

// The line breaker: a paragraph's glyphs, glue and penalties broken into
// lines by Knuth and Plass's total fit -- every line of a justified
// paragraph set to the column but the last, a forced break obeyed, and a
// ragged paragraph's lines left at their own width.

namespace layout = render::layout;

/// @brief A paragraph of so many ten-point words, a space between each.
static memory::Slice<layout::Node*> words(memory::Arena& arena, const int count, const int forced = -1) {
    std::vector<layout::Node*> nodes;
    for (int index = 0; index < count; ++index) {
        if (index > 0) {
            if (index == forced) {
                auto* pause = arena.compose<layout::Node>(layout::Node::Type::Penalty);
                pause->penalty({.value = -10000});
                nodes.push_back(pause);
            }
            auto* space = arena.compose<layout::Node>(layout::Node::Type::Glue);
            space->glue({.width = 3.33f, .stretch = 1.67f, .shrink = 1.11f});
            nodes.push_back(space);
        }
        auto* word = arena.compose<layout::Node>(layout::Node::Type::Box);
        word->box({.width = 20.0f, .height = 7.0f, .depth = 2.0f});
        nodes.push_back(word);
    }
    const memory::Slice<layout::Node*> slice = arena.allocate<layout::Node*>(nodes.size());
    for (std::size_t index = 0; index < nodes.size(); ++index) slice[index] = nodes[index];
    return slice;
}

int main() {
    memory::Arena arena(1u << 22);
    memory::Arena scratch(1u << 22);

    {
        const layout::Breaker breaker(arena, scratch, {.target = 100.0f});
        const memory::Slice<layout::Node*> lines = breaker.compose(words(arena, 20));
        assert((lines.size() >= 4) && "twenty words of a column's quarter each take several lines");
        bool justified = true;
        for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
            justified = justified && std::abs(lines[index]->box().width - 100.0f) < 0.01f;
        }
        assert((justified) && "every line but the last is set to the column");
        assert((lines[lines.size() - 1]->box().order == layout::Node::Order::Fil) &&
               "the last ends in \\parfillskip, so its words keep their natural spacing");
        bool clean = true;
        for (const layout::Node* line : lines) {
            const layout::Node* first = line->box().list.empty() ? nullptr : line->box().list[0];
            clean = clean && first && first->type != layout::Node::Type::Glue;
        }
        assert((clean) && "no line opens with the space it was broken at");
    }
    {
        const layout::Breaker breaker(arena, scratch, {.target = 1000.0f});
        assert((breaker.compose(words(arena, 6, 3)).size() == 2) && "a forced break ends a line however short");
        assert((breaker.compose(words(arena, 6)).size() == 1) && "and without one, what fits is one line");
    }
    {
        const layout::Breaker breaker(arena, scratch,
                                      {.target = 100.0f, .justification = layout::Node::Justification::Left});
        bool ragged = true;
        for (const layout::Node* line : breaker.compose(words(arena, 20))) {
            ragged = ragged && line->box().order == layout::Node::Order::Fil;
        }
        assert((ragged) && "a ragged line's room goes to the fill at its end, its word spaces keeping their width");

        // Every ragged line costs the same, so a tie goes to the later
        // break, as TeX's does: the first line takes all four words that
        // fit, not the first break it could.
        const memory::Slice<layout::Node*> filled = breaker.compose(words(arena, 6));
        std::size_t first = 0;
        for (const layout::Node* node : filled[0]->box().list) first += node->type == layout::Node::Type::Box;
        assert((filled.size() == 2 && first == 4) && "a ragged line filled before it breaks");
    }
    {
        const layout::Breaker breaker(arena, scratch, {.target = 100.0f});
        assert((breaker.compose({}).empty()) && "nothing breaks into no lines");
    }
    {
        // A penalty of ten thousand is no place to break, as TeX's \nobreak:
        // a line too long to end anywhere else runs on past it.
        std::vector<layout::Node*> nodes;
        for (int index = 0; index < 3; ++index) {
            if (index > 0) {
                auto* hold = arena.compose<layout::Node>(layout::Node::Type::Penalty);
                hold->penalty({.value = 10000});
                nodes.push_back(hold);
            }
            auto* word = arena.compose<layout::Node>(layout::Node::Type::Box);
            word->box({.width = 60.0f, .height = 7.0f});
            nodes.push_back(word);
        }
        const memory::Slice<layout::Node*> slice = arena.allocate<layout::Node*>(nodes.size());
        for (std::size_t index = 0; index < nodes.size(); ++index) slice[index] = nodes[index];
        const layout::Breaker breaker(arena, scratch, {.target = 100.0f});
        assert((breaker.compose(slice).size() == 1) && "a line ends at no penalty of ten thousand");
    }

    return 0;
}
