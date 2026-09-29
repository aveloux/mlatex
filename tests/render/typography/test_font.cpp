#include "engine.hpp"
#include "memory/arena.hpp"
#include "typography/library.hpp"
#include "typography/registry.hpp"
#include "typography/shaper.hpp"

#include <cassert>
#include <filesystem>

// A font: a face at a size -- a character's glyph, how far it moves the pen,
// how much ink it has, and the face's vertical metrics, all in points.

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

int main() {
    Fonts fonts;
    if (!fonts.text) return 0;
    const render::typography::Font& text = *fonts.text;

    const std::uint32_t letter = text.index('A');
    assert((letter != 0) && "a letter has a glyph");
    assert((text.advance(letter) > 0.0f && text.advance(letter) < text.size()) && "with an advance under an em");
    const render::typography::Font::Box ink = text.bounds(letter);
    assert((ink.width > 0.0f && ink.height > 0.0f) && "and ink");
    assert((text.index(0x10FFFD) == 0) && "a character the face lacks has none");

    const render::typography::Font* small = fonts.registry.get({.family = "text", .size = 5.0f});
    assert((small && std::abs(small->advance(letter) * 2.0f - text.advance(letter)) < 0.01f) &&
           "half the size is half the advance");

    const render::typography::Font::Metric metric = text.metrics();
    assert((metric.ascent > 0.0f && metric.descent < 0.0f) && "the face rises above its baseline and falls below");
    assert((metric.size == 10.0f) && "at the size it was built at");
    assert((text.family() == "lmroman10-regular" && text.face() != nullptr) &&
           "it knows its face and the file's own family, not the alias it was asked for by");

    return 0;
}
