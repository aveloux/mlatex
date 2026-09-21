#include "typography/font.hpp"
#include "logger.hpp"

#include <harfbuzz/hb-ot.h>
#include <utility>

namespace render::typography {

    Font::~Font() noexcept {
        dispose();
    }

    Font::Font(Font&& input) noexcept
        : parent(std::exchange(input.parent, nullptr)),
          handle(std::exchange(input.handle, nullptr)),
          points(std::exchange(input.points, 0.0f)),
          ascent(std::exchange(input.ascent, 0.0f)),
          descent(std::exchange(input.descent, 0.0f)),
          gap(std::exchange(input.gap, 0.0f)),
          cache(std::exchange(input.cache, {})) {}

    Font& Font::operator=(Font&& input) noexcept {
        if (this != &input) {
            dispose();
            parent = std::exchange(input.parent, nullptr);
            handle = std::exchange(input.handle, nullptr);
            points = std::exchange(input.points, 0.0f);
            ascent = std::exchange(input.ascent, 0.0f);
            descent = std::exchange(input.descent, 0.0f);
            gap = std::exchange(input.gap, 0.0f);
            cache = std::exchange(input.cache, {});
        }
        return *this;
    }

    bool Font::compose(const Face& face, const float size) noexcept {
        if (!face.hb()) {
            Logger::log(Logger::Type::Layout, Logger::Level::Error, "Invalid face instance for font composition");
            return false;
        }
        dispose();

        handle = hb_font_create(face.hb());
        if (!handle) {
            Logger::log(Logger::Type::Layout, Logger::Level::Error, "HarfBuzz font creation failed");
            return false;
        }

        const int scale = static_cast<int>(size * 64.0f);
        hb_font_set_scale(handle, scale, scale);
        hb_ot_font_set_funcs(handle);

        hb_font_extents_t extents{};
        if (hb_font_get_h_extents(handle, &extents)) {
            ascent = static_cast<float>(extents.ascender);
            descent = static_cast<float>(extents.descender);
            gap = static_cast<float>(extents.line_gap);
        }

        parent = &face;
        points = size;
        return true;
    }

    void Font::dispose() noexcept {
        if (handle) {
            hb_font_destroy(handle);
            handle = nullptr;
        }
        parent = nullptr;
        points = 0.0f;
        ascent = 0.0f;
        descent = 0.0f;
        gap = 0.0f;
        cache = {};
    }

    Font::Metric Font::metrics(const float scale) const noexcept {
        if (!handle || scale == 0.0f) return {};
        const float ratio = 1.0f / scale;
        return Metric{
            .ascent = ascent * ratio,
            .descent = descent * ratio,
            .gap = gap * ratio,
            .height = (ascent - descent) * ratio,
            .units = static_cast<float>(parent ? parent->units() : 0),
            .size = points
        };
    }

    Font::Box Font::bounds(const std::uint32_t glyph, const float scale) const noexcept {
        if (!handle || scale == 0.0f) return {};

        Slot& slot = cache[glyph & mask];
        if (!slot.ready || slot.glyph != glyph) {
            hb_glyph_extents_t extents{};
            if (!hb_font_get_glyph_extents(handle, glyph, &extents)) return {};
            slot = Slot{.glyph = glyph, .ready = true, .extents = extents};
        }

        const float ratio = 1.0f / scale;
        return Box{
            .x = static_cast<float>(slot.extents.x_bearing) * ratio,
            .y = static_cast<float>(slot.extents.y_bearing) * ratio,
            .width = static_cast<float>(slot.extents.width) * ratio,
            .height = static_cast<float>(slot.extents.height) * ratio
        };
    }

    std::uint32_t Font::glyph(const std::uint32_t codepoint) const noexcept {
        if (!handle) return 0;
        std::uint32_t result = 0;
        return hb_font_get_nominal_glyph(handle, codepoint, &result) ? result : 0;
    }

    float Font::advance(const std::uint32_t glyph, const float scale) const noexcept {
        if (!handle || scale == 0.0f) return 0.0f;
        return static_cast<float>(hb_font_get_glyph_h_advance(handle, glyph)) / scale;
    }

}