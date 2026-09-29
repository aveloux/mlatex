#pragma once

#include <string_view>

namespace render::graphics {

    /// @brief A color, as a document writes one: a hex triplet, an `rgb()`
    ///        or `rgba()` call, or a name.
    ///
    /// The three channels and the opacity are kept from 0 to 1, which is the
    /// form a page description wants and #parse converts into on the way in.
    struct Color {
        float r{0.0f};
        float g{0.0f};
        float b{0.0f};
        float alpha{1.0f};

        /// @brief Parses a color written any of the ways this engine reads.
        ///
        /// @par Formats
        /// @code
        /// FF0000        % a hex triplet, the leading '#' optional
        /// #FF0000CC     % the same, with a fourth pair for alpha
        /// rgb(255,0,0)  % three channels, 0 to 255
        /// rgba(255,0,0,0.8)   % the same, with alpha 0 to 1
        /// red           % a name -- see #black and its siblings
        /// @endcode
        ///
        /// @param text Color, in any of the forms above.
        /// @return The color, or black when none of them match.
        [[nodiscard]] static Color parse(std::string_view text) noexcept;

        /// @brief Parses a hex triplet or quadruplet.
        /// @param text Six or eight hex digits, with or without a leading
        ///             `#`; an eighth pair, if present, is alpha.
        /// @return The color, or black when the text does not match.
        [[nodiscard]] static Color hex(std::string_view text) noexcept;

        [[nodiscard]] friend bool operator==(const Color&, const Color&) noexcept = default;
    };

    // xcolor's nineteen base colors, exactly as it defines them, so a
    // document written for LaTeX gets the same red it got there.
    inline constexpr Color black{0.0f, 0.0f, 0.0f};
    inline constexpr Color white{1.0f, 1.0f, 1.0f};
    inline constexpr Color red{1.0f, 0.0f, 0.0f};
    inline constexpr Color green{0.0f, 1.0f, 0.0f};
    inline constexpr Color blue{0.0f, 0.0f, 1.0f};
    inline constexpr Color cyan{0.0f, 1.0f, 1.0f};
    inline constexpr Color magenta{1.0f, 0.0f, 1.0f};
    inline constexpr Color yellow{1.0f, 1.0f, 0.0f};
    inline constexpr Color gray{0.5f, 0.5f, 0.5f};
    inline constexpr Color darkgray{0.25f, 0.25f, 0.25f};
    inline constexpr Color lightgray{0.75f, 0.75f, 0.75f};
    inline constexpr Color brown{0.75f, 0.5f, 0.25f};
    inline constexpr Color lime{0.75f, 1.0f, 0.0f};
    inline constexpr Color olive{0.5f, 0.5f, 0.0f};
    inline constexpr Color orange{1.0f, 0.5f, 0.0f};
    inline constexpr Color pink{1.0f, 0.75f, 0.75f};
    inline constexpr Color purple{0.75f, 0.0f, 0.25f};
    inline constexpr Color teal{0.0f, 0.5f, 0.5f};
    inline constexpr Color violet{0.5f, 0.0f, 0.5f};

}
