/// @file
/// @brief Face implementation: open a font file that is already in memory.
///
/// Nothing here reads from disk and nothing copies. Collection holds the bytes, a
/// blob points at them, and HarfBuzz reads the table directory out of that.
#include "typography/face.hpp"
#include "logger.hpp"

#include <harfbuzz/hb-ot.h>

#include <utility>

namespace render::typography {

    Face::~Face() noexcept {
        dispose();
    }

    Face::Face(Face&& input) noexcept
        : face(std::exchange(input.face, nullptr)),
          storage(std::exchange(input.storage, {})),
          scale(std::exchange(input.scale, 1000.0f)),
          measure(std::exchange(input.measure, nullptr)),
          cache(std::exchange(input.cache, {})) {}

    Face& Face::operator=(Face&& input) noexcept {
        if (this != &input) {
            dispose();
            face = std::exchange(input.face, nullptr);
            storage = std::exchange(input.storage, {});
            scale = std::exchange(input.scale, 1000.0f);
            measure = std::exchange(input.measure, nullptr);
            cache = std::exchange(input.cache, {});
        }
        return *this;
    }

    void Face::dispose() noexcept {
        if (measure) {
            hb_font_destroy(measure);
            measure = nullptr;
        }
        if (face) {
            hb_face_destroy(face);
            face = nullptr;
        }
        storage = {};   // borrowed, so dropping the view is the whole cleanup
        cache = {};
    }

    std::size_t Face::count() const noexcept {
        return face ? hb_face_get_glyph_count(face) : 0;
    }

    hb_glyph_extents_t Face::extents(const std::uint32_t glyph) const noexcept {
        if (!face) return {};
        if (!measure) {
            // A font at the face's own scale -- HarfBuzz's default -- so what
            // it reports is in the file's units, the same for every size.
            measure = hb_font_create(face);
            hb_ot_font_set_funcs(measure);
            cache.resize(mask + 1);
        }

        Slot& slot = cache[glyph & mask];
        if (!slot.ready || slot.glyph != glyph) {
            hb_glyph_extents_t found{};
            if (!hb_font_get_glyph_extents(measure, glyph, &found)) found = {};
            slot = Slot{.glyph = glyph, .ready = true, .extents = found};
        }
        return slot.extents;
    }

    bool Face::compose(const std::span<const std::uint8_t> data) noexcept {
        if (data.empty()) return false;

        // Read-only and with no destructor: the bytes belong to the arena,
        // which outlives every face built over them. HarfBuzz therefore never
        // copies and never frees them.
        hb_blob_t* blob = hb_blob_create(
            reinterpret_cast<const char*>(data.data()), static_cast<unsigned int>(data.size()),
            HB_MEMORY_MODE_READONLY, nullptr, nullptr);
        if (!blob) return false;

        hb_face_t* opened = hb_face_create(blob, 0);
        hb_blob_destroy(blob);   // the face took its own reference

        if (!opened || hb_face_get_glyph_count(opened) == 0) {
            if (opened) hb_face_destroy(opened);
            Logger::log(Logger::Type::Layout, Logger::Level::Error,
                        "HarfBuzz rejected the font file");
            return false;
        }

        dispose();
        face = opened;
        storage = data;
        scale = static_cast<float>(hb_face_get_upem(opened));
        return true;
    }

}
