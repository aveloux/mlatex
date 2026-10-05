#include "engine.hpp"
#include "memory/arena.hpp"
#include "typography/collection.hpp"
#include "typography/registry.hpp"
#include "typography/shaper.hpp"

#include <cassert>
#include <filesystem>

// The collection: the font tree indexed once by file name, nothing read until
// a face is asked for, a face read once however often it is asked for, and
// found by a family or an alias whatever its case.

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
    const std::size_t indexed = collection.post((assets() / "fonts").string());
    assert((indexed > 0 && collection.faces == indexed) && "the font tree is indexed");
    assert((collection.bytes == 0) && "and nothing is read until asked for");

    collection.set("text", "lmroman10-regular");
    const render::typography::Collection::Entry* entry = collection.get("text");
    assert((entry != nullptr && !entry->bytes.empty()) && "an alias reads the face it names");
    assert((collection.bytes > 0) && "and its bytes are kept");

    const std::size_t resident = collection.bytes;
    assert((collection.get("TEXT") == entry) && "a family is found whatever its case");
    assert((collection.get("lmroman10-regular") == entry) && "and by its own name as well as the alias");
    assert((collection.bytes == resident) && "a second request reads nothing more");
    assert((collection.get("no-such-family") == nullptr) && "an unknown family is none");

    render::typography::Collection empty(arena);
    assert((empty.post((assets() / "no-such-folder").string()) == 0) &&
           "a folder that is not there indexes nothing");

    // The system's own folders, listed once: whatever they hold, a face the
    // engine carries keeps its name, and a second listing finds nothing new.
    const std::size_t count = collection.faces;
    const std::size_t added = collection.post();
    assert((collection.faces == count + added) && "the system's faces are indexed beside the carried ones");
    assert((collection.get("text") == entry) && "a carried face keeps its name");
    assert((collection.post() == 0) && "the system's folders are listed once");

    return 0;
}
