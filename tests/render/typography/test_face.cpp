#include "engine.hpp"
#include "memory/arena.hpp"
#include "typography/collection.hpp"
#include "typography/face.hpp"
#include "typography/registry.hpp"
#include "typography/shaper.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>

// A face: one font file opened with HarfBuzz, its glyphs counted and each
// measured once in the file's own units, whatever size it is set at.

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
    memory::Arena arena(1u << 24);
    render::typography::Collection collection(arena);
    collection.post((assets() / "fonts").string());
    const render::typography::Collection::Entry* entry = collection.get("lmroman10-regular");
    assert((entry != nullptr) && "the file is found");
    if (!entry) return 0;

    render::typography::Face face;
    assert((face.compose(entry->bytes)) && "a font file opens");
    assert((face.count() > 100) && "and has its glyphs");
    assert((face.units() >= 1000.0f) && "measured in the file's own units");
    assert((face.data().size() == entry->bytes.size()) && "it reads the bytes it was given, not a copy");

    const hb_glyph_extents_t first = face.extents(36);
    const hb_glyph_extents_t again = face.extents(36);
    assert((first.width == again.width && first.height == again.height) && "a glyph measures the same twice");

    render::typography::Face moved(std::move(face));
    assert((moved.handle() != nullptr && face.handle() == nullptr) &&
           "a face moves, and the one moved from is empty");
    moved.dispose();
    assert((moved.handle() == nullptr) && "a face disposed of holds nothing");

    constexpr std::array<std::uint8_t, 4> noise{1, 2, 3, 4};
    render::typography::Face broken;
    assert((!broken.compose(noise)) && "bytes that are no font do not open");

    return 0;
}
