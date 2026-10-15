#pragma once

namespace render::graphics {
    /// @brief A point on the page a canvas draws to.
    struct Point2 {
        float across{0.0f}; ///< To the right.
        float up{0.0f}; ///< Up the page.
    };

    /// @brief A point in space, before a Projection flattens it.
    struct Point3 {
        float across{0.0f}; ///< Along the x axis.
        float along{0.0f}; ///< Along the y axis.
        float up{0.0f}; ///< Along the z axis.
    };
}
