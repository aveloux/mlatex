#pragma once

namespace render::graphics {

    /// @brief A point on the page a canvas draws to.
    struct Point2 {
        float x{0.0f};
        float y{0.0f};
    };

    /// @brief A point in space, before a Projection flattens it.
    struct Point3 {
        float x{0.0f};
        float y{0.0f};
        float z{0.0f};
    };

}
