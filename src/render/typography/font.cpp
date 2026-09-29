/// @file
/// @brief Font implementation: a face scaled to one size.
///
/// HarfBuzz is set to 26.6 fixed point at compose() time, so everything it
/// returns is in sixty-fourths of a point. Dividing by that happens here and
/// nowhere else, which is why nothing outside this file carries a scale.
#include "typography/font.hpp"
#include "logger.hpp"

#include <harfbuzz/hb-ot.h>

#include <utility>

namespace render::typography {

    /// HarfBuzz's fixed-point denominator: 26.6 means sixty-fourths.
    static constexpr float grain = 1.0f / 64.0f;

    Font::~Font() noexcept {
        dispose();
    }

    Font::Font(Font&& input) noexcept
        : parent(std::exchange(input.parent, nullptr)),
          name(std::exchange(input.name, {})),
          shaper(std::exchange(input.shaper, nullptr)),
          points(std::exchange(input.points, 0.0f)),
          ascent(std::exchange(input.ascent, 0.0f)),
          descent(std::exchange(input.descent, 0.0f)),
          gap(std::exchange(input.gap, 0.0f)) {}

    Font& Font::operator=(Font&& input) noexcept {
        if (this != &input) {
            dispose();
            parent = std::exchange(input.parent, nullptr);
            name = std::exchange(input.name, {});
            shaper = std::exchange(input.shaper, nullptr);
            points = std::exchange(input.points, 0.0f);
            ascent = std::exchange(input.ascent, 0.0f);
            descent = std::exchange(input.descent, 0.0f);
            gap = std::exchange(input.gap, 0.0f);
        }
        return *this;
    }

    bool Font::compose(const Face& face, const float size, const std::string_view family) noexcept {
        if (!face.handle() || size <= 0.0f) {
            Logger::log(Logger::Type::Layout, Logger::Level::Error,
                        "Cannot build a font from an unopened face");
            return false;
        }

        dispose();

        shaper = hb_font_create(face.handle());
        if (!shaper) {
            Logger::log(Logger::Type::Layout, Logger::Level::Error, "HarfBuzz font creation failed");
            return false;
        }

        const int scale = static_cast<int>(size * 64.0f);
        hb_font_set_scale(shaper, scale, scale);
        hb_ot_font_set_funcs(shaper);

        // Read once here rather than per query: these three are the only
        // metrics a line ever needs, and they never change for a built font.
        hb_font_extents_t extents{};
        if (hb_font_get_h_extents(shaper, &extents)) {
            ascent = static_cast<float>(extents.ascender) * grain;
            descent = static_cast<float>(extents.descender) * grain;
            gap = static_cast<float>(extents.line_gap) * grain;
        }

        parent = &face;
        points = size;
        name = family;
        return true;
    }

    void Font::dispose() noexcept {
        if (shaper) {
            hb_font_destroy(shaper);
            shaper = nullptr;
        }
        parent = nullptr;
        name = {};
        points = 0.0f;
        ascent = 0.0f;
        descent = 0.0f;
        gap = 0.0f;
    }

    Font::Metric Font::metrics() const noexcept {
        if (!shaper) return {};
        return Metric{
            .ascent = ascent,
            .descent = descent,
            .gap = gap,
            .height = ascent - descent,
            .size = points
        };
    }

    Font::Box Font::bounds(const std::uint32_t glyph) const noexcept {
        if (!parent) return {};

        // The face keeps every glyph's extent in its own units, once for all
        // its sizes; this size is that, scaled.
        const hb_glyph_extents_t extents = parent->extents(glyph);
        const float scale = points / parent->units();
        return Box{
            .x = static_cast<float>(extents.x_bearing) * scale,
            .y = static_cast<float>(extents.y_bearing) * scale,
            .width = static_cast<float>(extents.width) * scale,
            // HarfBuzz reports height downwards from the bearing, so it is
            // negative for ink that sits above the baseline. Callers want a
            // span, not a direction.
            .height = -static_cast<float>(extents.height) * scale
        };
    }

    std::uint32_t Font::index(const std::uint32_t codepoint) const noexcept {
        if (!shaper) return 0;
        std::uint32_t glyph = 0;
        return hb_font_get_nominal_glyph(shaper, codepoint, &glyph) ? glyph : 0;
    }

    float Font::advance(const std::uint32_t glyph) const noexcept {
        if (!shaper) return 0.0f;
        return static_cast<float>(hb_font_get_glyph_h_advance(shaper, glyph)) * grain;
    }

}
