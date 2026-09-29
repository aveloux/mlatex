#pragma once

#include "typography/face.hpp"

#include <harfbuzz/hb.h>

#include <array>
#include <cstdint>
#include <string_view>

namespace render::typography {

    /// @brief A face at one size: what the engine measures glyphs against.
    ///
    /// Everything a Font reports is in points, scaled from the design units in
    /// the file. Glyphs are addressed by *glyph index*, not by code point --
    /// the two are different numbers, and confusing them is how a document
    /// ends up full of boxes. index() converts one to the other; shaping
    /// produces indices directly.
    ///
    /// @par Use
    /// @code
    /// const std::uint32_t glyph = font.index(0x03B4);   // the code point for delta
    /// if (glyph == 0) { /* this face has no delta */ }
    ///
    /// const float width = font.advance(glyph);
    /// const Font::Box shape = font.bounds(glyph);
    /// @endcode
    class Font {
    public:
        /// @brief Vertical metrics, in points.
        struct Metric {
            float ascent{0.0f};    ///< Above the baseline, positive.
            float descent{0.0f};   ///< Below the baseline, negative as the file reports it.
            float gap{0.0f};       ///< Extra leading the file asks for between lines.
            float height{0.0f};    ///< Ascent less descent: the em's full extent.
            float size{0.0f};      ///< The size this font was built at.
        };

        /// @brief One glyph's ink extent, in points, relative to its origin.
        struct Box {
            float x{0.0f};        ///< Left bearing.
            float y{0.0f};        ///< Top of the ink, above the baseline.
            float width{0.0f};    ///< Ink width.
            float height{0.0f};   ///< Ink height.
        };

        Font() noexcept = default;
        ~Font() noexcept;

        Font(const Font&) = delete("a font owns its HarfBuzz font, which would be destroyed twice");
        Font& operator=(const Font&) = delete("a font owns its HarfBuzz font, which would be destroyed twice");
        Font(Font&& input) noexcept;
        Font& operator=(Font&& input) noexcept;

        /// @brief Builds a font from a face at a size.
        /// @param face   Opened file; must outlive this object.
        /// @param size   Size in points.
        /// @param family Name this face answers to, so that a caller wanting
        ///               the same face at another size can ask for it without
        ///               having kept the name; must outlive this object.
        /// @return True when HarfBuzz accepted the face.
        [[nodiscard]] bool compose(const Face& face, float size, std::string_view family = {}) noexcept;

        /// @brief Releases the HarfBuzz font, leaving the face alone.
        void dispose() noexcept;

        /// @brief This font's vertical metrics, in points.
        /// @complexity O(1); the numbers are read once at compose() time.
        [[nodiscard]] Metric metrics() const noexcept;

        /// @brief One glyph's ink extent, in points.
        /// @param glyph Glyph index, as index() or shaping produced it.
        /// @return Its box, or a zero box when the face has no such glyph.
        /// @complexity O(1) once the face has measured the glyph at any size.
        [[nodiscard]] Box bounds(std::uint32_t glyph) const noexcept;

        /// @brief The glyph a code point maps to.
        /// @param codepoint Unicode scalar value.
        /// @return Its glyph index, or 0 when this face does not cover it.
        ///         Zero is the face's own "missing" glyph, so a caller that
        ///         wants a different font for the character must test for it.
        /// @complexity O(1).
        [[nodiscard]] std::uint32_t index(std::uint32_t codepoint) const noexcept;

        /// @brief How far the pen moves past a glyph, in points.
        /// @param glyph Glyph index.
        /// @complexity O(1).
        [[nodiscard]] float advance(std::uint32_t glyph) const noexcept;

        /// @brief The HarfBuzz font, for shaping and for the math tables.
        [[nodiscard]] hb_font_t* handle() const noexcept { return shaper; }

        /// @brief The face this font reads.
        [[nodiscard]] const Face* face() const noexcept { return parent; }

        /// @brief The size this font was built at, in points.
        [[nodiscard]] float size() const noexcept { return points; }

        /// @brief The family this font was built from.
        ///
        /// Asking the registry for `font.family()` at another size is how a
        /// superscript gets a real smaller font rather than a shrunken box.
        [[nodiscard]] std::string_view family() const noexcept { return name; }

    private:
        const Face* parent{nullptr};   ///< The file this font reads.
        std::string_view name{};       ///< Family this face answers to.
        hb_font_t* shaper{nullptr};    ///< HarfBuzz font, scaled to #points.
        float points{0.0f};            ///< Size in points.

        float ascent{0.0f};            ///< Above the baseline, in points.
        float descent{0.0f};           ///< Below the baseline, in points.
        float gap{0.0f};               ///< Extra leading, in points.
    };

}
