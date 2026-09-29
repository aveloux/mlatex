#include "engine.hpp"
#include "memory/arena.hpp"
#include "typography/library.hpp"
#include "typography/registry.hpp"
#include "typography/shaper.hpp"

#include <cassert>
#include <filesystem>

// The library: the font tree indexed once by file name, nothing read until
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
    memory::Arena arena(1u << 24);
    render::typography::Library library(arena);
    const std::size_t indexed = library.survey((assets() / "fonts").string());
    assert((indexed > 0 && library.count() == indexed) && "the font tree is indexed");
    assert((library.resident() == 0) && "and nothing is read until asked for");

    library.alias("text", "lmroman10-regular");
    const render::typography::Library::Entry* entry = library.read("text");
    assert((entry != nullptr && !entry->bytes.empty()) && "an alias reads the face it names");
    assert((library.resident() > 0) && "and its bytes are kept");

    const std::size_t resident = library.resident();
    assert((library.read("TEXT") == entry) && "a family is found whatever its case");
    assert((library.read("lmroman10-regular") == entry) && "and by its own name as well as the alias");
    assert((library.resident() == resident) && "a second request reads nothing more");
    assert((library.read("no-such-family") == nullptr) && "an unknown family is none");

    render::typography::Library empty(arena);
    assert((empty.survey((assets() / "no-such-folder").string()) == 0) &&
           "a folder that is not there indexes nothing");

    // The system's own folders, listed once: whatever they hold, a face the
    // engine carries keeps its name, and a second listing finds nothing new.
    const std::size_t count = library.count();
    const std::size_t added = library.system();
    assert((library.count() == count + added) && "the system's faces are indexed beside the carried ones");
    assert((library.read("text") == entry) && "a carried face keeps its name");
    assert((library.system() == 0) && "the system's folders are listed once");

    return 0;
}
