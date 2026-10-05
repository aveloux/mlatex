/// @file
/// @brief Face implementation: open a font file that is already in memory.
///
/// Nothing here reads from disk and nothing copies. Collection holds the bytes, a
/// blob points at them, and HarfBuzz reads the table directory out of that.
#include "typography/face.hpp"
#include "logger.hpp"

#include <harfbuzz/hb-ot.h>

#include <algorithm>
#include <cmath>
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
            // The ink is what the outline draws. A glyph's own box in the
            // file counts every point it holds, and some faces keep a
            // contour of one point, an anchor, far above or below the ink --
            // Noto Naskh's tatweel reaches from under the baseline to over
            // the x-height that way, its stroke a hair's breadth on the
            // baseline. HarfBuzz closes such a contour with a line back to
            // where it began, which goes nowhere: only a stroke that goes
            // somewhere counts, from where it starts to where it ends.
            struct Ink {
                float left{0.0f};     // the outline's leftmost point
                float bottom{0.0f};   // its lowest
                float right{0.0f};    // its rightmost
                float top{0.0f};      // its highest
                bool drawn{false};    // whether it has drawn any point yet

                // One point the outline passes through or bends toward: a
                // control point's box holds its curve, as the file's own
                // box is reckoned.
                void post(const float x, const float y) noexcept {
                    if (!drawn) *this = Ink{.left = x, .bottom = y, .right = x, .top = y, .drawn = true};
                    left = std::min(left, x);
                    bottom = std::min(bottom, y);
                    right = std::max(right, x);
                    top = std::max(top, y);
                }
            };
            static hb_draw_funcs_t* const pen = [] {
                hb_draw_funcs_t* made = hb_draw_funcs_create();
                hb_draw_funcs_set_line_to_func(
                    made,
                    [](hb_draw_funcs_t*, void* data, hb_draw_state_t* state, const float x, const float y, void*) {
                        if (x == state->current_x && y == state->current_y) return;
                        static_cast<Ink*>(data)->post(state->current_x, state->current_y);
                        static_cast<Ink*>(data)->post(x, y);
                    },
                    nullptr, nullptr);
                hb_draw_funcs_set_quadratic_to_func(
                    made,
                    [](hb_draw_funcs_t*, void* data, hb_draw_state_t* state, const float bendx, const float bendy,
                       const float x, const float y, void*) {
                        if (x == state->current_x && y == state->current_y && bendx == x && bendy == y) return;
                        static_cast<Ink*>(data)->post(state->current_x, state->current_y);
                        static_cast<Ink*>(data)->post(bendx, bendy);
                        static_cast<Ink*>(data)->post(x, y);
                    },
                    nullptr, nullptr);
                hb_draw_funcs_set_cubic_to_func(
                    made,
                    [](hb_draw_funcs_t*, void* data, hb_draw_state_t* state, const float firstx, const float firsty,
                       const float secondx, const float secondy, const float x, const float y, void*) {
                        if (x == state->current_x && y == state->current_y && firstx == x && firsty == y &&
                            secondx == x && secondy == y) {
                            return;
                        }
                        static_cast<Ink*>(data)->post(state->current_x, state->current_y);
                        static_cast<Ink*>(data)->post(firstx, firsty);
                        static_cast<Ink*>(data)->post(secondx, secondy);
                        static_cast<Ink*>(data)->post(x, y);
                    },
                    nullptr, nullptr);
                hb_draw_funcs_make_immutable(made);
                return made;
            }();

            Ink ink;
            hb_font_draw_glyph(measure, glyph, pen, &ink);
            hb_glyph_extents_t found{};
            if (ink.drawn) {
                // Rounded to the unit as HarfBuzz rounds its own, so a glyph
                // with no stray point measures as it always did.
                const auto left = static_cast<hb_position_t>(std::lround(ink.left));
                const auto top = static_cast<hb_position_t>(std::lround(ink.top));
                found = hb_glyph_extents_t{
                    .x_bearing = left,
                    .y_bearing = top,
                    .width = static_cast<hb_position_t>(std::lround(ink.right)) - left,
                    .height = static_cast<hb_position_t>(std::lround(ink.bottom)) - top,
                };
            } else if (!hb_font_get_glyph_extents(measure, glyph, &found)) {
                // No outline -- a bitmap, or nothing at all: the file's box.
                found = {};
            }
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
