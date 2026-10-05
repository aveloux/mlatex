#include "engine.hpp"
#include "memory/arena.hpp"
#include "typography/collection.hpp"
#include "typography/registry.hpp"
#include "typography/shaper.hpp"

#include <cassert>
#include <filesystem>

// The registry: a face built at a size once and handed out again for the
// same request, another size another font from the same opened file.

/// The engine's assets directory, found from this file.
static std::filesystem::path assets() {
    return engine::locate(__FILE__);
}

/// The body face and the maths face at ten points, and what built them.
struct Fonts {
    memory::Arena arena{1u << 24};                            ///< What the faces and nodes take.
    memory::Arena scratch{1u << 22};                          ///< What a layout pass takes and drops.
    render::typography::Collection collection{arena};          ///< The indexed tree.
    render::typography::Registry registry{arena, collection}; ///< The faces at their sizes.
    const render::typography::Shaper shaper{arena};           ///< Text into glyphs.
    const render::typography::Font* text{nullptr};            ///< Latin Modern Roman.
    const render::typography::Font* maths{nullptr};           ///< New Computer Modern Math.

    Fonts() {
        collection.post((assets() / "fonts").string());
        collection.set("text", "lmroman10-regular");
        collection.set("expression", "NewCMMath-Regular");
        text = registry.get({.family = "text", .size = 10.0f});
        maths = registry.get({.family = "expression", .size = 10.0f});
        assert(text && maths && "the engine's faces open");
    }
};

int main() {
    Fonts fonts;
    if (!fonts.text) return 0;

    render::typography::Registry& registry = fonts.registry;
    assert((registry.get({.family = "text", .size = 10.0f}) == fonts.text) && "the same request is the same font");

    const render::typography::Font* small = registry.get({.family = "text", .size = 8.0f});
    assert((small && small != fonts.text && small->size() == 8.0f) && "another size is another font");
    assert((small && small->family() == fonts.text->family()) && "of the same family");
    assert((registry.faces == 2) && "two sizes of one file and the maths face open two files");
    assert((registry.get({.family = "no-such-family"}) == nullptr) && "an unknown family builds nothing");

    // A character the face lacks is drawn by its own design's face for that
    // script, in the cut in hand, at its size: Cyrillic, Greek and Hebrew by
    // New Computer Modern, Devanagari by its faces for that.
    const render::typography::Font* cyrillic = registry.cover(*fonts.text, 0x0416);
    assert((cyrillic && cyrillic->family() == "newcm10-regular" && cyrillic->size() == 10.0f &&
            cyrillic->index(0x0416) != 0) && "Cyrillic is Computer Modern's own, at the same size");
    assert((registry.cover(*fonts.text, 0x0436) == cyrillic) && "a second letter of the block is a lookup");
    assert((registry.cover(*fonts.text, 0x03B1) == cyrillic) && "Greek is in the same face");
    const render::typography::Font* bold = registry.get({.family = "lmroman10-bold", .size = 10.0f});
    const render::typography::Font* heavy = bold ? registry.cover(*bold, 0x0416) : nullptr;
    assert((heavy && heavy->family() == "newcm10-bold") && "bold text's Cyrillic is bold");
    const render::typography::Font* sans = registry.get({.family = "lmsans10-regular", .size = 10.0f});
    const render::typography::Font* plain = sans ? registry.cover(*sans, 0x0416) : nullptr;
    assert((plain && plain->family() == "newcmsans10-regular") && "sans text's Cyrillic is sans");
    const render::typography::Font* devanagari = registry.cover(*fonts.text, 0x0915);
    assert((devanagari && devanagari->family() == "newcm10devanagari-regular") && "Devanagari has faces of its own");
    assert((registry.cover(*fonts.text, 0x0627, false) == nullptr) &&
           "Arabic is in no face the engine carries, and the system's are not looked in unless asked");

    return 0;
}
