/// @file
/// @brief Shaper implementation: text to positioned layout nodes.
///
/// HarfBuzz does the shaping; the work here is turning its output into the
/// nodes the line breaker reads, and finding a face for anything the first
/// one cannot draw.
#include "typography/shaper.hpp"
#include "typography/registry.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace render::typography {

    Shaper::~Shaper() noexcept {
        if (buffer) hb_buffer_destroy(buffer);
        for (const Plan& kept : plans) {
            if (kept.plan) hb_shape_plan_destroy(kept.plan);
        }
    }

    memory::Slice<layout::Node*> Shaper::shape(
        const memory::Slice<const Font*> fonts,
        const std::string_view text,
        const memory::Slice<Feature> features
    ) const {
        if (text.empty() || fonts.empty()) return {};

        const Font* primary = nullptr;
        for (std::size_t index = 0; index < fonts.count; ++index) {
            if (fonts[index] && fonts[index]->handle()) {
                primary = fonts[index];
                break;
            }
        }
        if (!primary) return {};

        const memory::Slice<hb_feature_t> list = arena.allocate<hb_feature_t>(features.count);
        for (std::size_t index = 0; index < features.count; ++index) {
            list[index] = features[index].feature;
        }

        // One character read out of the UTF-8 at a byte, and how many bytes
        // it took. A malformed byte reads as itself over one byte, so a bad
        // encoding cannot stall anything that walks the text.
        const auto decode = [](const std::string_view from, const std::size_t at) -> std::pair<std::uint32_t, std::size_t> {
            const auto* data = reinterpret_cast<const std::uint8_t*>(from.data() + at);
            const std::size_t rest = from.size() - at;
            if ((data[0] & 0xE0) == 0xC0 && rest >= 2) {
                return {static_cast<std::uint32_t>(((data[0] & 0x1F) << 6) | (data[1] & 0x3F)), 2};
            }
            if ((data[0] & 0xF0) == 0xE0 && rest >= 3) {
                return {static_cast<std::uint32_t>(((data[0] & 0x0F) << 12) | ((data[1] & 0x3F) << 6) | (data[2] & 0x3F)), 3};
            }
            if ((data[0] & 0xF8) == 0xF0 && rest >= 4) {
                return {static_cast<std::uint32_t>(((data[0] & 0x07) << 18) | ((data[1] & 0x3F) << 12) |
                                                   ((data[2] & 0x3F) << 6) | (data[3] & 0x3F)), 4};
            }
            return {data[0], 1};
        };

        // One run, in one face, as HarfBuzz sets it: its script, its
        // language and its direction guessed from the text, so an Arabic
        // word joins and comes back in the order it is drawn in, right to
        // left. Whether any character came back as glyph zero -- the face
        // saying it cannot draw it -- is noted on the way.
        bool missing = false;
        const auto set = [&](const Font& font, const std::string_view run) -> memory::Slice<layout::Node*> {
            if (buffer) {
                hb_buffer_reset(buffer);
            } else {
                buffer = hb_buffer_create();
            }
            hb_buffer_add_utf8(buffer, run.data(), static_cast<int>(run.size()), 0, static_cast<int>(run.size()));
            hb_buffer_guess_segment_properties(buffer);

            // A number reads left to right in any script. Arabic's digits,
            // ٢٠٢٤, are Arabic's by Unicode, and a run of them alone would
            // otherwise come back turned round, as the script's words do.
            bool numeric = !run.empty();
            for (std::size_t at = 0; numeric && at < run.size();) {
                const auto [code, size] = decode(run, at);
                numeric = (code >= '0' && code <= '9') || (code >= 0x0660 && code <= 0x066C) ||
                          (code >= 0x06F0 && code <= 0x06F9) || code == '.' || code == ',';
                at += std::max<std::size_t>(size, 1);
            }
            if (numeric) hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);

            // Without features, the plan comes from the few kept here, found
            // by face and by what the buffer is written in; with them,
            // HarfBuzz finds or makes its own.
            if (list.empty()) {
                hb_segment_properties_t properties{};
                hb_buffer_get_segment_properties(buffer, &properties);
                hb_face_t* face = hb_font_get_face(font.handle());

                hb_shape_plan_t* chosen = nullptr;
                for (const Plan& kept : plans) {
                    if (kept.face == face && hb_segment_properties_equal(&kept.properties, &properties)) {
                        chosen = kept.plan;
                        break;
                    }
                }
                if (!chosen) {
                    Plan& slot = plans[oldest];
                    oldest = (oldest + 1) % plans.size();
                    if (slot.plan) hb_shape_plan_destroy(slot.plan);
                    chosen = hb_shape_plan_create_cached(face, &properties, nullptr, 0, nullptr);
                    slot = Plan{.face = face, .properties = properties, .plan = chosen};
                }
                hb_shape_plan_execute(chosen, font.handle(), buffer, nullptr, 0);
            } else {
                hb_shape(font.handle(), buffer, list.data, static_cast<unsigned int>(list.count));
            }

            unsigned int count = 0;
            const hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buffer, &count);
            const hb_glyph_position_t* position = hb_buffer_get_glyph_positions(buffer, &count);

            // The buffer is kept for the next run; see the member's notes.
            if (count == 0) return {};

            // Every glyph stands as tall and as deep as the face asked for
            // says, whichever face drew it, so a word in another script does
            // not push its line further from the next than the rest are.
            const memory::Slice<layout::Node*> nodes = arena.allocate<layout::Node*>(count);
            const Font::Metric metric = primary->metrics();
            const bool backward = HB_DIRECTION_IS_BACKWARD(hb_buffer_get_direction(buffer));
            constexpr float grain = 1.0f / 64.0f;

            for (std::size_t step = 0; step < count; ++step) {
                const std::uint32_t cluster = info[step].cluster;
                const auto [code, size] = cluster < run.size() ? decode(run, cluster) : std::pair<std::uint32_t, std::size_t>{0, 0};
                const float advance = static_cast<float>(position[step].x_advance) * grain;

                // A space becomes glue, not a glyph: it is where the breaker
                // is allowed to split the line, and it has to be able to
                // stretch. The proportions are TeX's: half the space may be
                // added, a third of it taken away.
                if (code == ' ') {
                    auto* node = arena.compose<layout::Node>(layout::Node::Type::Glue);
                    node->glue({
                        .width = advance,
                        .stretch = advance * 0.5f,
                        .shrink = advance / 3.0f
                    });
                    nodes[step] = node;
                    continue;
                }
                if (info[step].codepoint == 0) missing = true;

                // One glyph for a cluster of several characters is a
                // ligature: `fi`, `ffl`. It keeps them all, so the page's
                // text layer reads the word as written. A cluster drawn with
                // several glyphs -- a letter and a mark set on it -- keeps
                // each glyph's own. A run set right to left comes back in
                // the order it is drawn, its clusters running backwards, so
                // the cluster after this one is looked for behind it.
                std::string_view letters;
                const bool alone = (step == 0 || info[step - 1].cluster != cluster) &&
                                   (step + 1 == count || info[step + 1].cluster != cluster);
                if (alone) {
                    std::size_t end = run.size();
                    if (backward) {
                        for (std::size_t other = step; other-- > 0;) {
                            if (info[other].cluster > cluster) {
                                end = info[other].cluster;
                                break;
                            }
                        }
                    } else {
                        for (std::size_t other = step + 1; other < count; ++other) {
                            if (info[other].cluster > cluster) {
                                end = info[other].cluster;
                                break;
                            }
                        }
                    }
                    if (end > cluster + size) letters = arena.copy(run.substr(cluster, end - cluster));
                }

                auto* node = arena.compose<layout::Node>(layout::Node::Type::Glyph);
                node->glyph({
                    .width = advance,
                    .height = metric.ascent,
                    .depth = -metric.descent,
                    .x = static_cast<float>(position[step].x_offset) * grain,
                    .y = static_cast<float>(position[step].y_offset) * grain,
                    .code = info[step].codepoint,
                    .point = code,
                    .font = &font,
                    .letters = letters
                });
                nodes[step] = node;
            }
            return nodes;
        };

        const memory::Slice<layout::Node*> whole = set(*primary, text);
        if (!missing || (fonts.count < 2 && !registry)) return whole;

        // Something the face cannot draw. The text is cut into runs by the
        // face that draws each character -- the one asked for wherever it
        // can, then its own design's faces for another script, then the
        // fallbacks offered, then the system's -- and each run is shaped
        // whole in its own face, so a Cyrillic word keeps its kerning and an
        // Arabic one joins, as neither would glyph by glyph. A mark goes with
        // the letter it sits on, and a joiner with the letter before it.
        std::vector<std::pair<std::size_t, const Font*>> starts;
        hb_unicode_funcs_t* unicode = hb_unicode_funcs_get_default();
        for (std::size_t at = 0; at < text.size();) {
            const auto [code, size] = decode(text, at);
            const hb_unicode_general_category_t category =
                hb_unicode_general_category(unicode, code);
            const bool attached = category == HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK ||
                                  category == HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK ||
                                  category == HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK ||
                                  category == HB_UNICODE_GENERAL_CATEGORY_FORMAT;

            // A letter of another script is looked for in its own design's
            // faces before any fallback offered, so Greek text is set in
            // Computer Modern's Greek rather than the formulas'; a symbol,
            // which is what a fallback is offered for, in the fallback first.
            const bool letter = category == HB_UNICODE_GENERAL_CATEGORY_LOWERCASE_LETTER ||
                                category == HB_UNICODE_GENERAL_CATEGORY_UPPERCASE_LETTER ||
                                category == HB_UNICODE_GENERAL_CATEGORY_TITLECASE_LETTER ||
                                category == HB_UNICODE_GENERAL_CATEGORY_MODIFIER_LETTER ||
                                category == HB_UNICODE_GENERAL_CATEGORY_OTHER_LETTER;
            const Font* face = primary;
            if (attached && !starts.empty()) {
                face = starts.back().second;
            } else if (code > ' ' && primary->index(code) == 0) {
                face = registry && letter ? registry->cover(*primary, code, false) : nullptr;
                for (std::size_t index = 0; !face && index < fonts.count; ++index) {
                    const Font* offered = fonts[index];
                    if (offered && offered != primary && offered->handle() && offered->index(code) != 0) face = offered;
                }
                if (!face && registry) face = registry->cover(*primary, code, true);
                if (!face) face = primary;
            }

            if (starts.empty() || starts.back().second != face) starts.emplace_back(at, face);
            at += size;
        }
        if (starts.size() == 1 && starts.front().second == primary) return whole;

        std::vector<layout::Node*> gathered;
        gathered.reserve(text.size());
        for (std::size_t index = 0; index < starts.size(); ++index) {
            const std::size_t end = index + 1 < starts.size() ? starts[index + 1].first : text.size();
            const memory::Slice<layout::Node*> part = set(*starts[index].second, text.substr(starts[index].first, end - starts[index].first));
            gathered.insert(gathered.end(), part.begin(), part.end());
        }

        const memory::Slice<layout::Node*> nodes = arena.allocate<layout::Node*>(gathered.size());
        std::ranges::copy(gathered, nodes.begin());
        return nodes;
    }

}
