#include "engine.hpp"
#include "layout/node.hpp"
#include "memory/arena.hpp"
#include "typography/library.hpp"
#include "typography/registry.hpp"
#include "typography/shaper.hpp"

#include <cassert>
#include <filesystem>

// The shaper: text into glyphs with the face's own ligatures and kerning, a
// space into glue, and a character the first face lacks taken from the next.

/// The engine's assets directory, found from this file.
static std::filesystem::path assets() {
    return engine::locate(__FILE__);
}

/// The body face and the maths face at ten points, and what built them.
struct Fonts {
    memory::Arena arena{1u << 24};                            ///< What the faces and nodes take.
    memory::Arena scratch{1u << 22};                          ///< What a layout pass takes and drops.
    render::typography::Library library{arena};                ///< The indexed tree.
    render::typography::Registry registry{arena, library};    ///< The faces at their sizes.
    const render::typography::Shaper shaper{arena};           ///< Text into glyphs.
    const render::typography::Font* text{nullptr};            ///< Latin Modern Roman.
    const render::typography::Font* maths{nullptr};           ///< New Computer Modern Math.

    Fonts() {
        library.survey((assets() / "fonts").string());
        library.alias("text", "lmroman10-regular");
        library.alias("expression", "NewCMMath-Regular");
        text = registry.get({.family = "text", .size = 10.0f});
        maths = registry.get({.family = "expression", .size = 10.0f});
        assert(text && maths && "the engine's faces open");
    }
};

namespace layout = render::layout;

/// @brief How many of a run's nodes are of one kind.
static std::size_t kind(const memory::Slice<layout::Node*> nodes, const layout::Node::Type type) {
    std::size_t found = 0;
    for (const layout::Node* node : nodes) found += node && node->type == type;
    return found;
}

int main() {
    Fonts fonts;
    if (!fonts.text) return 0;
    const render::typography::Shaper& shaper = fonts.shaper;
    const render::typography::Font* text[] = {fonts.text};
    const memory::Slice<const render::typography::Font*> one{text, 1uz};

    const auto office = shaper.shape(one, "office", {});
    assert((kind(office, layout::Node::Type::Glyph) < 6) && "the face's ligatures join letters");
    bool spelled = false;
    for (const layout::Node* node : office) spelled = spelled || node->glyph().letters == "ffi";
    assert((spelled) && "and a ligature still spells the letters it stands for");

    assert((kind(shaper.shape(one, "\xC3\xA9t\xC3\xA9", {}), layout::Node::Type::Glyph) == 3) &&
           "a character past ASCII is one glyph");
    assert((kind(shaper.shape(one, "a b", {}), layout::Node::Type::Glue) == 1) && "a space is glue");

    float plain = 0.0f;
    for (const layout::Node* node : shaper.shape(one, "AV", {})) plain += node->glyph().width;
    const float apart = fonts.text->advance(fonts.text->index('A')) + fonts.text->advance(fonts.text->index('V'));
    assert((plain < apart) && "a kerned pair sits closer than its letters' own advances");

    const render::typography::Font* both[] = {fonts.text, fonts.maths};
    const auto sum = shaper.shape(memory::Slice{both, 2uz}, "\xE2\x88\x91", {});
    assert((kind(sum, layout::Node::Type::Glyph) == 1 && sum[0]->glyph().font == fonts.maths) &&
           "a character the first face lacks comes from the next");
    assert((shaper.shape(one, "", {}).empty()) && "no text is no glyphs");

    // Another script, through the registry: each run in the face that draws
    // it, shaped whole, every glyph as tall as the face asked for says.
    const render::typography::Shaper covering(fonts.arena, &fonts.registry);
    const auto russian = covering.shape(one, "\xD0\x96\xD1\x83\xD0\xBA", {});
    bool drawn = kind(russian, layout::Node::Type::Glyph) == 3;
    for (const layout::Node* node : russian) {
        drawn = drawn && node->glyph().code != 0 && node->glyph().font->family() == "newcm10-regular" &&
                node->glyph().height == fonts.text->metrics().ascent;
    }
    assert((drawn) && "a Cyrillic word is drawn by Computer Modern's Cyrillic, at the height of the text's face");
    const auto mixed = covering.shape(one, "a \xD0\x96 b", {});
    assert((kind(mixed, layout::Node::Type::Glyph) == 3 && kind(mixed, layout::Node::Type::Glue) == 2 &&
            mixed[0]->glyph().font == fonts.text && mixed[2]->glyph().font != fonts.text &&
            mixed[4]->glyph().font == fonts.text) && "a run is cut where the face that draws it changes");
    const auto missing = shaper.shape(one, "\xD0\x96", {});
    assert((kind(missing, layout::Node::Type::Glyph) == 1 && missing[0]->glyph().code == 0) &&
           "without a registry, a character no face offered draws is the face's box for none");

    return 0;
}
