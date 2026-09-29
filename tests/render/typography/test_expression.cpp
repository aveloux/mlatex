#include "engine.hpp"
#include "memory/arena.hpp"
#include "typography/expression.hpp"
#include "typography/library.hpp"
#include "typography/registry.hpp"
#include "typography/shaper.hpp"

#include <cassert>
#include <filesystem>

// The maths table: the numbers a formula is built from, read from the
// face's OpenType MATH table, and the larger sizes of a glyph -- whole, or
// built from the face's own parts once no single size is tall enough.

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
    if (!fonts.maths) return 0;
    const render::typography::Font& maths = *fonts.maths;

    const render::typography::Expression table(maths);
    assert((table.present()) && "the maths face has a maths table");
    assert((!render::typography::Expression(*fonts.text).present()) && "the text face has none");

    const render::typography::Expression::Metric metric = table.metrics();
    assert((metric.axis > 0.0f && metric.thickness > 0.0f) && "the axis and the bar are set");
    assert((metric.gap > 0.0f && metric.clearance > metric.gap) &&
           "a displayed radical stands clearer than one in a line");
    assert((metric.rule > 0.0f && metric.raise > 0.0f && metric.raise < 1.0f) &&
           "the radical's own numbers are read");

    const std::uint32_t radical = maths.index(0x221A);
    const render::typography::Expression::Variant base = table.stretch(radical, 1.0f);
    const render::typography::Expression::Variant tall = table.stretch(radical, 30.0f);
    assert((base.glyph == radical) && "a short reach takes the base size");
    assert((tall.glyph != radical && tall.advance >= 30.0f) && "a tall one takes a taller size");
    const render::typography::Expression::Variant tallest = table.stretch(radical, 1000.0f);
    assert((tallest.advance < 1000.0f && tallest.advance >= tall.advance) && "past every size, the tallest");

    const render::typography::Expression::Assembly built = table.assemble(radical, 120.0f);
    assert((built.pieces.size() >= 3) && "a height past every size is built from parts");
    assert((built.span >= 119.9f) && "and reaches it");
    bool rising = !built.pieces.empty() && built.pieces.front().lift == 0.0f;
    for (std::size_t index = 1; index < built.pieces.size(); ++index) {
        rising = rising && built.pieces[index].lift > built.pieces[index - 1].lift;
    }
    assert((rising) && "each piece stands above the last, the first at the bottom");
    assert((table.assemble(radical, 300.0f).pieces.size() > built.pieces.size()) &&
           "a taller height takes more pieces");
    assert((table.assemble(maths.index('x'), 100.0f).pieces.empty()) && "a letter is built from nothing");

    return 0;
}
