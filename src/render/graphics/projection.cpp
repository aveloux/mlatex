/// @file
/// @brief Projection implementation: rotate into view, then drop the depth.
#include "render/graphics/projection.hpp"

#include <cmath>
#include <numbers>

namespace render::graphics {
    /// @brief Rotates a point by azimuth about z, then by elevation about
    ///        the axis that rotation left as x, and drops what is left as
    ///        depth.
    /// @param point     Point to rotate.
    /// @param elevation Degrees up from the x-y plane.
    /// @param azimuth   Degrees around z, applied first.
    /// @return The point's x and y in the rotated frame; its z is depth,
    ///         which an orthographic view never draws.
    [[nodiscard]] static Point2 rotate(
        const Point3 &point,
        const float elevation,
        const float azimuth
    ) noexcept {
        constexpr float turn = std::numbers::pi_v<float> / 180.0f;
        const float turned = azimuth * turn;
        const float tilted = elevation * turn;

        const float across = point.across * std::cos(turned) - point.along * std::sin(turned);
        const float along = point.across * std::sin(turned) + point.along * std::cos(turned);

        return {.across = across, .up = along * std::cos(tilted) - point.up * std::sin(tilted)};
    }

    Projection Projection::planar() noexcept {
        return orthographic(0.0f, 0.0f);
    }

    Projection Projection::isometric() noexcept {
        // Elevation here is measured from looking straight down the z axis
        // -- planar() is zero -- so the tilt that foreshortens all three axes
        // alike is acos(1 / sqrt 3), the complement of the 35.26 degrees a
        // drafting text measures from the horizon.
        return orthographic(54.735610317245345f, 45.0f);
    }

    Projection Projection::orthographic(const float elevation, const float azimuth) noexcept {
        Projection view;
        view.across = rotate({.across = 1.0f}, elevation, azimuth);
        view.along = rotate({.along = 1.0f}, elevation, azimuth);
        view.up = rotate({.up = 1.0f}, elevation, azimuth);
        return view;
    }

    Point2 Projection::project(const Point3 &point) const noexcept {
        return {
            .across = point.across * across.across + point.along * along.across + point.up * up.across,
            .up = point.across * across.up + point.along * along.up + point.up * up.up,
        };
    }
}
