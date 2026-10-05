#include "engine.hpp"
#include "layout/paragraph.hpp"
#include "memory/arena.hpp"
#include "typography/collection.hpp"
#include "typography/registry.hpp"
#include "typography/shaper.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>

// A paragraph: its material broken into lines against a column, the lines
// stacked a baseline apart into one column of its own, inside whatever
// margins it was given -- and laid out again against the same column for
// nothing.

/// The engine's assets directory, found from this file.
static std::filesystem::path assets() {
    return engine::locate(__FILE__);
}

/// The body face and the maths face at ten points, and what built them.
struct Fonts {
    memory::Arena arena{1u << 24};                            ///< What the faces and nodes take.
    memory::Arena scratch{1u << 22};                          ///< What a layout pass takes and drops.
    render::typography::Collection collection{arena};          ///< The indexed tree.
    render::typography::Registry registry{arena, collection}; ///< The faces at their sizes.
    const render::typography::Shaper shaper{arena};           ///< Text into glyphs.
    const render::typography::Font* text{nullptr};            ///< Latin Modern Roman.
    const render::typography::Font* maths{nullptr};           ///< New Computer Modern Math.

    Fonts() {
        collection.post((assets() / "fonts").string());
        collection.set("text", "lmroman10-regular");
        collection.set("expression", "NewCMMath-Regular");
        text = registry.get({.family = "text", .size = 10.0f});
        maths = registry.get({.family = "expression", .size = 10.0f});
        assert(text && maths && "the engine's faces open");
    }
};

namespace layout = render::layout;

int main() {
    Fonts fonts;
    if (!fonts.text) return 0;

    const render::typography::Font* text[] = {fonts.text};
    const memory::Slice<layout::Node*> material = fonts.shaper.shape(
        memory::Slice{text, 1uz},
        "A paragraph long enough to be broken into several lines, so that each of them can be measured "
        "against the column it was set in, and the whole stacked a baseline apart.",
        {});

    layout::Paragraph paragraph(fonts.arena, material, layout::Node::Justification::Full, 0.0f, 0.0f);
    assert((paragraph.node() == nullptr) && "a paragraph not yet laid out has no lines");
    paragraph.layout(fonts.scratch, 150.0f, 12.0f);
    const layout::Node* column = paragraph.node();
    assert((column && column->box().alignment == layout::Node::Alignment::Vertical) && "laid out, it is a column");

    std::size_t lines = 0;
    for (const layout::Node* line : column->box().list) lines += line->type == layout::Node::Type::Box;
    assert((lines >= 3) && "of several lines");
    assert((std::abs(column->box().list[0]->box().width - 150.0f) < 0.01f) && "each as wide as the column");

    paragraph.layout(fonts.scratch, 150.0f, 12.0f);
    assert((paragraph.node() == column) && "laid out again against the same column, it is the same lines");
    paragraph.layout(fonts.scratch, 300.0f, 12.0f);
    assert((paragraph.node() != column) && "against another, it is broken again");

    layout::Paragraph inset(fonts.arena, material, layout::Node::Justification::Full, 20.0f, 30.0f);
    inset.layout(fonts.scratch, 150.0f, 12.0f);
    const layout::Node* first = inset.node()->box().list[0];
    assert((std::abs(first->box().width - 100.0f) < 0.01f) &&
           "set in from its margins, a line is as wide as they leave");
    assert((first->box().offset == 20.0f) && "and moved in by the left one");

    // Right to left, a line is moved in by its right margin, where it starts;
    // words in Latin letters in it still read left to right among themselves.
    layout::Paragraph reversed(fonts.arena, material, layout::Node::Justification::Full, 20.0f, 30.0f, true);
    reversed.layout(fonts.scratch, 150.0f, 12.0f);
    const layout::Node* opening = reversed.node()->box().list[0];
    assert((std::abs(opening->box().width - 100.0f) < 0.01f && opening->box().offset == 30.0f) &&
           "read right to left, a line is moved in by the right margin");
    assert((opening->box().list[0]->type == layout::Node::Type::Glyph &&
            opening->box().list[0]->glyph().point == 'A') && "and a run of Latin words in it keeps their order");

    return 0;
}
