#include "layout/node.hpp"
#include "memory/arena.hpp"
#include "render/graphics/canvas.hpp"
#include "render/graphics/projection.hpp"

#include <cassert>

// The canvas a picture is drawn on: lines in its own coordinates, read up
// from the bottom left as graph paper is, and lines in space flattened by a
// projection -- composed into one box that takes as much room as it
// declared, as it drew, or the larger of the two.

namespace graphics = render::graphics;
namespace layout = render::layout;

/// @brief How many strokes a canvas's box holds.
static std::size_t strokes(const layout::Node* box) {
    std::size_t found = 0;
    for (const layout::Node* child : box->box().list) found += child && child->type == layout::Node::Type::Path;
    return found;
}

int main() {
    memory::Arena arena(1u << 20);

    // Inside its declared size, every way of measuring agrees.
    {
        graphics::Canvas canvas(100.0f, 50.0f);
        canvas.line({0.0f, 0.0f}, {100.0f, 50.0f});
        canvas.line({0.0f, 50.0f}, {100.0f, 0.0f}, graphics::Color{1.0f, 0.0f, 0.0f, 1.0f}, 2.0f, true);
        const layout::Node* box = canvas.compose(arena, graphics::Canvas::Room::Declared);
        assert((box && box->type == layout::Node::Type::Box && box->box().absolute) &&
               "a canvas is a box of its own");
        assert((box->box().width == 100.0f && box->box().depth == 50.0f && box->box().height == 0.0f) &&
               "as large as it declared, hanging from its top edge as any block does");
        assert((strokes(box) == 2) && "holding each stroke");

        bool styled = false;
        for (const layout::Node* child : box->box().list) {
            styled = styled || (child->path().dashed && child->path().width == 2.0f && child->path().color.r == 1.0f);
        }
        assert((styled) && "each stroke keeps its color, width and dashes");
    }

    // Drawn past its declared size.
    {
        graphics::Canvas canvas(10.0f, 10.0f);
        canvas.line({0.0f, 0.0f}, {100.0f, 100.0f});
        const layout::Node* pinned = canvas.compose(arena, graphics::Canvas::Room::Declared);
        const layout::Node* grown = canvas.compose(arena, graphics::Canvas::Room::Grown);
        assert((pinned->box().width == 10.0f) && "an overlay keeps its declared size whatever it draws");
        assert((grown->box().width >= 100.0f && grown->box().depth >= 100.0f) &&
               "a picture in the text grows to take in all it drew");
    }

    // A TikZ picture declares nothing, and is exactly what it draws.
    {
        graphics::Canvas canvas(0.0f, 0.0f);
        canvas.line({10.0f, 10.0f}, {40.0f, 30.0f});
        const layout::Node* box = canvas.compose(arena, graphics::Canvas::Room::Drawn);
        assert((box->box().width >= 29.9f && box->box().width <= 31.1f) && "a drawing as wide as its strokes");
    }

    // A line in space, flattened.
    {
        graphics::Canvas canvas(50.0f, 50.0f);
        canvas.line(graphics::Point3{0.0f, 0.0f, 0.0f}, graphics::Point3{10.0f, 10.0f, 10.0f},
                    graphics::Projection::isometric());
        assert((strokes(canvas.compose(arena, graphics::Canvas::Room::Grown)) == 1) &&
               "a line in space is one stroke");
    }

    // Nothing drawn is still a box.
    {
        const graphics::Canvas canvas(20.0f, 20.0f);
        const layout::Node* box = canvas.compose(arena, graphics::Canvas::Room::Declared);
        assert((box && strokes(box) == 0 && box->box().width == 20.0f) &&
               "an empty canvas is an empty box of its size");
    }

    return 0;
}
