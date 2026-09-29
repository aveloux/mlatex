#include "render/graphics/projection.hpp"

#include <cassert>
#include <cmath>

// Three dimensions flattened onto two: the plane, which drops the depth; an
// isometric view, which foreshortens all three axes alike; and an
// orthographic view turned by two angles.

namespace graphics = render::graphics;

int main() {
    const graphics::Projection planar = graphics::Projection::planar();
    const graphics::Point2 flat = planar.project({2.0f, 3.0f, 5.0f});
    assert((flat.x == 2.0f && flat.y == 3.0f) && "the plane drops the depth");

    const graphics::Projection isometric = graphics::Projection::isometric();
    const auto length = [&isometric](const graphics::Point3 point) {
        const graphics::Point2 image = isometric.project(point);
        return std::hypot(image.x, image.y);
    };
    const float across = length({1.0f, 0.0f, 0.0f});
    assert((across > 0.0f) && "an isometric view shows each axis");
    assert((std::abs(length({0.0f, 1.0f, 0.0f}) - across) < 0.001f &&
           std::abs(length({0.0f, 0.0f, 1.0f}) - across) < 0.001f) && "and foreshortens all three alike");

    const graphics::Projection turned = graphics::Projection::orthographic(0.0f, 0.0f);
    const graphics::Point2 origin = turned.project({0.0f, 0.0f, 0.0f});
    assert((origin.x == 0.0f && origin.y == 0.0f) && "every view keeps the origin where it is");
    const graphics::Point2 unturned = turned.project({1.0f, 2.0f, 3.0f});
    assert((std::abs(unturned.x - 1.0f) < 0.001f && std::abs(unturned.y - 2.0f) < 0.001f) &&
           "a view turned by nothing is the plane");

    return 0;
}
