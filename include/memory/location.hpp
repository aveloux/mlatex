#pragma once

#include <cstdint>

namespace memory {
    /// @brief A position in source text, kept for diagnostics.
    ///
    /// Lines and columns count from one. A location that is zero in both
    /// fields means no position is known, which is what a default-constructed
    /// location says.
    struct Location {
        std::uint32_t line{0}; ///< Line number, from one.
        std::uint32_t column{0}; ///< Column number, from one.

        /// Which file the line is in, by the number syntax::Mouth::files
        /// gives its name; zero for text that is no file's.
        std::uint32_t file{0};
    };
}
