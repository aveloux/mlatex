/// @file
/// @brief Expression implementation: the OpenType MATH table in points.
///
/// HarfBuzz reports these constants in the font's own scaled units, which for
/// a Font built here means 26.6 fixed point. The three percentage constants are
/// the exception: they come back as whole percent and are turned into plain
/// fractions, because that is how the typesetter multiplies by them.
#include "typography/expression.hpp"

#include <harfbuzz/hb-ot.h>

#include <algorithm>
#include <limits>

namespace render::typography {

    /// HarfBuzz's fixed-point denominator: 26.6 means sixty-fourths.
    static constexpr float grain = 1.0f / 64.0f;

    Expression::Expression(const Font& font) noexcept : parent(font) {}

    bool Expression::present() const noexcept {
        return parent.handle() && hb_ot_math_has_data(hb_font_get_face(parent.handle()));
    }

    Expression::Metric Expression::metrics() const noexcept {
        if (ready) return cache;
        if (!present()) return {};

        hb_font_t* handle = parent.handle();

        const auto constant = [handle](const hb_ot_math_constant_t which) {
            return static_cast<float>(hb_ot_math_get_constant(handle, which)) * grain;
        };

        cache = Metric{
            .axis = constant(HB_OT_MATH_CONSTANT_AXIS_HEIGHT),
            .numerator = constant(HB_OT_MATH_CONSTANT_FRACTION_NUMERATOR_DISPLAY_STYLE_SHIFT_UP),
            .denominator = constant(HB_OT_MATH_CONSTANT_FRACTION_DENOMINATOR_DISPLAY_STYLE_SHIFT_DOWN),
            .ascent = constant(HB_OT_MATH_CONSTANT_FRACTION_NUMERATOR_SHIFT_UP),
            .descent = constant(HB_OT_MATH_CONSTANT_FRACTION_DENOMINATOR_SHIFT_DOWN),
            .upper = constant(HB_OT_MATH_CONSTANT_STACK_TOP_SHIFT_UP),

            // TeX keeps one clearance for both parts; a font may state two,
            // and the wider is the one neither part may come inside.
            .separation = std::max(constant(HB_OT_MATH_CONSTANT_FRACTION_NUMERATOR_GAP_MIN),
                                   constant(HB_OT_MATH_CONSTANT_FRACTION_DENOMINATOR_GAP_MIN)),
            .distance = std::max(constant(HB_OT_MATH_CONSTANT_FRACTION_NUM_DISPLAY_STYLE_GAP_MIN),
                                 constant(HB_OT_MATH_CONSTANT_FRACTION_DENOM_DISPLAY_STYLE_GAP_MIN)),
            .spacing = constant(HB_OT_MATH_CONSTANT_STACK_GAP_MIN),
            .spread = constant(HB_OT_MATH_CONSTANT_STACK_DISPLAY_STYLE_GAP_MIN),
            .thickness = constant(HB_OT_MATH_CONSTANT_FRACTION_RULE_THICKNESS),
            .gap = constant(HB_OT_MATH_CONSTANT_RADICAL_VERTICAL_GAP),
            .clearance = constant(HB_OT_MATH_CONSTANT_RADICAL_DISPLAY_STYLE_VERTICAL_GAP),
            .rule = constant(HB_OT_MATH_CONSTANT_RADICAL_RULE_THICKNESS),
            .ascender = constant(HB_OT_MATH_CONSTANT_RADICAL_EXTRA_ASCENDER),
            .before = constant(HB_OT_MATH_CONSTANT_RADICAL_KERN_BEFORE_DEGREE),
            .after = constant(HB_OT_MATH_CONSTANT_RADICAL_KERN_AFTER_DEGREE),
            .raise = static_cast<float>(
                hb_ot_math_get_constant(handle, HB_OT_MATH_CONSTANT_RADICAL_DEGREE_BOTTOM_RAISE_PERCENT)) / 100.0f,
            .subscript = constant(HB_OT_MATH_CONSTANT_SUBSCRIPT_SHIFT_DOWN),
            .superscript = constant(HB_OT_MATH_CONSTANT_SUPERSCRIPT_SHIFT_UP),

            // These are percentages of the base size, not distances, so
            // they arrive unscaled and are turned into the fractions the
            // typesetter multiplies a size by.
            .script = static_cast<float>(
                hb_ot_math_get_constant(handle, HB_OT_MATH_CONSTANT_SCRIPT_PERCENT_SCALE_DOWN)) / 100.0f,
            .scriptscript = static_cast<float>(
                hb_ot_math_get_constant(handle, HB_OT_MATH_CONSTANT_SCRIPT_SCRIPT_PERCENT_SCALE_DOWN)) / 100.0f,
            .display = constant(HB_OT_MATH_CONSTANT_DISPLAY_OPERATOR_MIN_HEIGHT),
            .drop = constant(HB_OT_MATH_CONSTANT_SUPERSCRIPT_BASELINE_DROP_MAX),
            .sink = constant(HB_OT_MATH_CONSTANT_SUBSCRIPT_BASELINE_DROP_MIN),
            .limit = constant(HB_OT_MATH_CONSTANT_UPPER_LIMIT_GAP_MIN)
        };

        ready = true;
        return cache;
    }

    float Expression::correction(const std::uint32_t glyph) const noexcept {
        if (!parent.handle()) return 0.0f;
        return static_cast<float>(
            hb_ot_math_get_glyph_italics_correction(parent.handle(), glyph)) * grain;
    }

    Expression::Variant Expression::stretch(const std::uint32_t glyph, const float height) const noexcept {
        if (!parent.handle()) return Variant{.glyph = glyph, .advance = height};

        // Eight is past what any shipped font offers for one delimiter, so the
        // list comes back whole and the search below is over all of it.
        hb_ot_math_glyph_variant_t variants[8];
        unsigned int count = 8;
        hb_ot_math_get_glyph_variants(parent.handle(), glyph, HB_DIRECTION_TTB, 0, &count, variants);

        for (unsigned int index = 0; index < count; ++index) {
            if (const float reach = static_cast<float>(variants[index].advance) * grain; reach >= height) {
                return Variant{.glyph = variants[index].glyph, .advance = reach};
            }
        }

        // Nothing tall enough: the tallest variant if there was one, otherwise
        // the glyph as written. Either way the caller gets something to draw.
        if (count > 0) {
            return Variant{
                .glyph = variants[count - 1].glyph,
                .advance = static_cast<float>(variants[count - 1].advance) * grain
            };
        }
        return Variant{.glyph = glyph, .advance = parent.bounds(glyph).height};
    }

    Expression::Assembly Expression::assemble(const std::uint32_t glyph, const float height) const {
        Assembly built;
        hb_font_t* handle = parent.handle();
        if (!handle) return built;

        // Sixteen is past what any shipped font uses for one assembly: a
        // radical has three parts and a brace five.
        hb_ot_math_glyph_part_t parts[16];
        unsigned int count = 16;
        hb_ot_math_get_glyph_assembly(handle, glyph, HB_DIRECTION_TTB, 0, &count, parts, nullptr);
        if (count == 0) return built;

        const float least = static_cast<float>(hb_ot_math_get_min_connector_overlap(handle, HB_DIRECTION_TTB)) * grain;

        // What the pieces add up to with each extender drawn once, split into
        // the parts drawn once whatever the height and the parts repeated.
        float fixed = 0.0f;
        float extending = 0.0f;
        std::size_t singles = 0;
        std::size_t extenders = 0;
        for (unsigned int index = 0; index < count; ++index) {
            const float advance = static_cast<float>(parts[index].full_advance) * grain;
            if (parts[index].flags & HB_OT_MATH_GLYPH_PART_FLAG_EXTENDER) {
                extending += advance;
                ++extenders;
            } else {
                fixed += advance;
                ++singles;
            }
        }

        // The fewest repeats that reach the height with the pieces overlapping
        // as little as the font allows. Bounded, so a font whose extenders are
        // all overlap cannot spin this forever.
        const auto longest = [&](const std::size_t repeats) {
            const std::size_t pieces = singles + repeats * extenders;
            return fixed + static_cast<float>(repeats) * extending -
                   static_cast<float>(pieces > 0 ? pieces - 1 : 0) * least;
        };
        std::size_t repeats = extenders > 0 ? 1 : 0;
        while (extenders > 0 && longest(repeats) < height && repeats < 256) ++repeats;

        std::vector<const hb_ot_math_glyph_part_t*> order;
        order.reserve(singles + repeats * extenders);
        for (unsigned int index = 0; index < count; ++index) {
            const bool extender = parts[index].flags & HB_OT_MATH_GLYPH_PART_FLAG_EXTENDER;
            for (std::size_t copy = 0; copy < (extender ? repeats : 1); ++copy) order.push_back(&parts[index]);
        }
        if (order.empty()) return built;

        // One overlap for every joint: as much as brings the whole down to
        // the height, but never less than the font's least nor more than the
        // shortest connector at any joint allows.
        float total = 0.0f;
        float most = std::numeric_limits<float>::max();
        for (std::size_t index = 0; index < order.size(); ++index) {
            total += static_cast<float>(order[index]->full_advance) * grain;
            if (index + 1 < order.size()) {
                const auto shared = std::min(order[index]->end_connector_length, order[index + 1]->start_connector_length);
                most = std::min(most, static_cast<float>(shared) * grain);
            }
        }

        const std::size_t joints = order.size() - 1;
        float overlap = 0.0f;
        if (joints > 0) {
            overlap = std::clamp((total - height) / static_cast<float>(joints), least, std::max(least, most));
        }

        float lift = 0.0f;
        built.pieces.reserve(order.size());
        for (const hb_ot_math_glyph_part_t* part : order) {
            built.pieces.push_back(Piece{.glyph = part->glyph, .lift = lift});
            lift += static_cast<float>(part->full_advance) * grain - overlap;
        }
        built.span = total - static_cast<float>(joints) * overlap;
        return built;
    }

}
