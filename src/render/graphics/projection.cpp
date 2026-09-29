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
        const Point3& point,
        const float elevation,
        const float azimuth
    ) noexcept {
        constexpr float turn = std::numbers::pi_v<float> / 180.0f;
        const float a = azimuth * turn;
        const float e = elevation * turn;

        const float x = point.x * std::cos(a) - point.y * std::sin(a);
        const float y = point.x * std::sin(a) + point.y * std::cos(a);

        return {x, y * std::cos(e) - point.z * std::sin(e)};
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
        return Projection(
            rotate({1.0f, 0.0f, 0.0f}, elevation, azimuth),
            rotate({0.0f, 1.0f, 0.0f}, elevation, azimuth),
            rotate({0.0f, 0.0f, 1.0f}, elevation, azimuth));
    }

    Point2 Projection::project(const Point3& point) const noexcept {
        return {
            point.x * x.x + point.y * y.x + point.z * z.x,
            point.x * x.y + point.y * y.y + point.z * z.y,
        };
    }

}
