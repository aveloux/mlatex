#pragma once

#include "render/graphics/point.hpp"

namespace render::graphics {

    /// @brief Flattens three dimensions onto two, the way a drafting board does.
    ///
    /// Not a camera with any depth to it: there is no perspective divide and
    /// nothing gets nearer or farther, which is what "orthographic" means. A
    /// projection is entirely stated by where each of the three axes ends up
    /// on the page -- three 2D vectors -- and a point is their sum, weighted
    /// by its own three coordinates. That sum is the whole of project(), so a
    /// point costs the same six multiplies and four adds however the view was
    /// built.
    ///
    /// @par Language
    /// @code
    /// \spaceline{0}{0}{0}{1}{1}{1}   % projected with whatever view is current
    /// @endcode
    class Projection {
    public:
        /// @brief Looking straight down the z axis: the plain 2D case.
        [[nodiscard]] static Projection planar() noexcept;

        /// @brief The classic drafting view: every axis foreshortened alike,
        ///        120 degrees from each of the other two on the page.
        [[nodiscard]] static Projection isometric() noexcept;

        /// @brief A general orthographic view.
        /// @param elevation Degrees the view is tilted from looking straight
        ///                  down the z axis: 0 is planar(), 90 a side view.
        /// @param azimuth   Degrees the view is turned around the z axis first.
        [[nodiscard]] static Projection orthographic(float elevation, float azimuth) noexcept;

        /// @brief Flattens one point.
        /// @param point Point in space.
        /// @return Where it falls on the page, in the projection's own units.
        /// @complexity O(1).
        [[nodiscard]] Point2 project(const Point3& point) const noexcept;

    private:
        /// @brief A view stated by where each axis's unit step lands.
        /// @param across Where a unit step on x lands.
        /// @param along  Where a unit step on y lands.
        /// @param up     Where a unit step on z lands.
        Projection(const Point2 across, const Point2 along, const Point2 up) noexcept
            : x(across), y(along), z(up) {}

        Point2 x{1.0f, 0.0f};   ///< Where a unit step on x lands.
        Point2 y{0.0f, 1.0f};   ///< Where a unit step on y lands.
        Point2 z{0.0f, 0.0f};   ///< Where a unit step on z lands.
    };

}
