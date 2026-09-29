#include "render/graphics/color.hpp"

#include <cassert>
#include <cmath>

// Colors as a document writes them: a hex triplet or quadruplet, CSS's
// rgb() and rgba(), and xcolor's names -- and black for anything else.

namespace graphics = render::graphics;

/// @brief Two colors the same to within a rounding.
static bool same(const graphics::Color& one, const graphics::Color& other) {
    return std::abs(one.r - other.r) < 0.01f && std::abs(one.g - other.g) < 0.01f &&
           std::abs(one.b - other.b) < 0.01f && std::abs(one.alpha - other.alpha) < 0.01f;
}

int main() {
    assert((same(graphics::Color::parse("#FF0000"), {1.0f, 0.0f, 0.0f, 1.0f})) && "a hex triplet");
    assert((same(graphics::Color::parse("00FF00"), {0.0f, 1.0f, 0.0f, 1.0f})) && "a hex triplet without its mark");
    assert((same(graphics::Color::parse("#0000FF80"), {0.0f, 0.0f, 1.0f, 0.5f})) && "a hex quadruplet carries alpha");
    assert((same(graphics::Color::parse("rgb(255, 128, 0)"), {1.0f, 0.5f, 0.0f, 1.0f})) && "rgb()");
    assert((same(graphics::Color::parse("rgba(0,0,0,0.25)"), {0.0f, 0.0f, 0.0f, 0.25f})) && "rgba()");
    assert((same(graphics::Color::parse("red"), {1.0f, 0.0f, 0.0f, 1.0f})) && "xcolor's red");
    assert((same(graphics::Color::parse("teal"), {0.0f, 0.5f, 0.5f, 1.0f})) && "xcolor's teal");
    assert((graphics::Color::parse("grey") == graphics::Color::parse("gray")) && "grey is gray");
    assert((graphics::Color::parse("  blue  ") == graphics::Color::parse("blue")) &&
           "spaces around a name are nothing");
    assert((graphics::Color::parse("nonsense") == graphics::black) && "anything else is black");
    assert((graphics::Color::parse("rgb(1,2)") == graphics::black) && "and so is a call short of a channel");
    assert((graphics::Color::parse("#12345") == graphics::black) && "and a hex of the wrong length");
    return 0;
}
