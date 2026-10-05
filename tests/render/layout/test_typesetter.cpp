#include "layout/line.hpp"
#include "layout/typesetter.hpp"
#include "memory/arena.hpp"
#include "render/primitives/expressions.hpp"
#include "syntax/cursor.hpp"
#include "syntax/expression/parser.hpp"
#include "syntax/expression/unicodes.hpp"
#include "syntax/lexicon.hpp"
#include "syntax/mouth.hpp"
#include "syntax/semantics/union.hpp"
#include "typography/collection.hpp"
#include "typography/expression.hpp"
#include "typography/registry.hpp"
#include "typography/shaper.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

// The typesetter: a formula's tree lowered into boxes. Its hardest case is
// the radical, which has to look the way a reader expects one to, whatever
// it covers. Every radical below is laid out and then measured the way the page
// draws it -- every box's offset and shift applied -- and held to what the
// OpenType MATH table asks of one:
//
// - the sign's top meets the bar, and the bar starts where the sign ends;
// - the bar stands clear of the contents by at least the font's own gap;
// - the sign reaches down at least as far as the contents do;
// - the sign grows with its contents, built from the font's parts once no
//   single size is tall enough;
// - a sign taller than it needs to be shares what it has to spare evenly
//   above and below the contents, rather than hanging it all under the line;
// - the contents stand clear of the sign sideways, a lone letter as much as
//   a longer term.

namespace layout = render::layout;
namespace typography = render::typography;

/// @brief An inked rectangle on the page, measured downwards.
struct Ink {
    float left{0.0f};     ///< Left edge.
    float right{0.0f};    ///< Right edge.
    float top{0.0f};      ///< Top edge; smaller is higher.
    float bottom{0.0f};   ///< Bottom edge.
};

/// @brief Everything a formula drew, sorted into what a radical is made of.
struct Drawing {
    std::vector<Ink> sign{};     ///< The outermost radical sign's pieces.
    std::size_t pieces{0};       ///< How many glyphs drew that sign.
    std::vector<Ink> glyphs{};   ///< Every other glyph.
    std::vector<Ink> rules{};    ///< Every bar.
};

/// @brief The glyph's ink, where the page draws it.
static Ink ink(const layout::Node::Glyph& mark, const float x, const float baseline) {
    const typography::Font::Box box = mark.font->bounds(mark.code);
    const float origin = baseline + mark.y;
    return Ink{
        .left = x + mark.x + box.x,
        .right = x + mark.x + box.x + box.width,
        .top = origin - box.y,
        .bottom = origin - box.y + box.height,
    };
}

/// @brief The height a column steps down by to reach a child's reference point.
static float height(const layout::Node* item) {
    switch (item->type) {
        case layout::Node::Type::Box: return item->box().height;
        case layout::Node::Type::Glyph: return item->glyph().height;
        case layout::Node::Type::Rule: return item->rule().height;
        default: return 0.0f;
    }
}

/// @brief Walks a box the way the composer draws it, collecting every mark.
static void walk(const layout::Node* node, const float x, const float baseline, Drawing& drawing) {
    if (!node) return;

    switch (node->type) {
        case layout::Node::Type::Glyph:
            drawing.glyphs.push_back(ink(node->glyph(), x, baseline));
            return;
        case layout::Node::Type::Rule: {
            const layout::Node::Rule& bar = node->rule();
            drawing.rules.push_back({.left = x, .right = x + bar.width, .top = baseline - bar.height,
                                     .bottom = baseline + bar.depth});
            return;
        }
        case layout::Node::Type::Box:
            break;
        default:
            return;
    }

    const layout::Node::Box& box = node->box();
    const float left = x + box.offset;
    const float line = baseline + box.shift;

    // The first radical sign met is the outermost one: a canvas whose first
    // glyph says it is a radical holds every piece of it.
    if (box.absolute) {
        const bool radical = drawing.sign.empty() && box.list.count > 0 &&
                             box.list[0]->type == layout::Node::Type::Glyph &&
                             box.list[0]->glyph().point == 0x221A;
        for (const layout::Node* child : box.list) {
            if (radical && child->type == layout::Node::Type::Glyph) {
                drawing.sign.push_back(ink(child->glyph(), left, line));
                ++drawing.pieces;
            } else {
                walk(child, left, line, drawing);
            }
        }
        return;
    }

    if (box.alignment == layout::Node::Alignment::Horizontal) {
        float pen = left;
        for (const layout::Node* child : box.list) {
            walk(child, pen, line, drawing);
            pen += layout::Line::advance(child);
        }
        return;
    }

    float down = line;
    for (const layout::Node* child : box.list) {
        if (!child) continue;
        down += height(child);
        walk(child, left, down, drawing);
        down += layout::Line::extent(child) - height(child);
    }
}

/// @brief The smallest rectangle around several.
static Ink around(const std::vector<Ink>& inks) {
    Ink whole{.left = std::numeric_limits<float>::max(), .right = std::numeric_limits<float>::lowest(),
              .top = std::numeric_limits<float>::max(), .bottom = std::numeric_limits<float>::lowest()};
    for (const Ink& part : inks) {
        whole.left = std::min(whole.left, part.left);
        whole.right = std::max(whole.right, part.right);
        whole.top = std::min(whole.top, part.top);
        whole.bottom = std::max(whole.bottom, part.bottom);
    }
    return whole;
}

/// @brief What one radical measured.
struct Measure {
    bool found{false};         ///< A sign and its bar were both drawn.
    float span{0.0f};          ///< The sign's height, top to bottom.
    std::size_t pieces{0};     ///< How many glyphs drew the sign.
    float meeting{0.0f};       ///< How far the sign's top is from the bar's.
    float joint{0.0f};         ///< How far the bar starts from the sign's ink.
    float above{0.0f};         ///< From the bar's underside down to the contents' ink.
    float below{0.0f};         ///< From the contents' ink bottom down to the sign's.
    float beside{0.0f};        ///< From the sign's ink across to the contents'.
};

/// @brief Lays a formula out and measures its outermost radical.
static Measure measure(const std::string& formula, const bool displayed, const typography::Font& font,
                       const layout::Typesetter& typesetter, memory::Arena& arena) {
    syntax::Lexicon lexicon(arena);
    syntax::semantics::Union state{};
    const syntax::expression::Unicodes unicodes;
    syntax::Mouth mouth(syntax::Cursor(std::vector<syntax::Token>{}), state, lexicon, arena);
    mouth.ingest(arena.copy(formula + "$"));

    const render::primitives::Expressions expressions(lexicon);
    syntax::expression::Parser parser(mouth, unicodes, expressions.grammar, arena,
                                      displayed ? syntax::expression::Node::Style::Display
                                                : syntax::expression::Node::Style::Inline);
    const syntax::expression::Node* tree = parser.parse('$');
    assert((tree != nullptr) && "the formula parses");
    if (!tree) return {};

    const layout::Node* box = typesetter.lower(tree, font, 0.0f);
    assert((box != nullptr) && "the formula lowers into a box");
    if (!box) return {};

    Drawing drawing;
    walk(box, 0.0f, 0.0f, drawing);
    if (drawing.sign.empty() || drawing.rules.empty()) return {};

    const Ink sign = around(drawing.sign);

    // The radical's own bar is the rule whose top is nearest the sign's.
    const Ink bar = *std::ranges::min_element(drawing.rules, {}, [&sign](const Ink& rule) {
        return std::abs(rule.top - sign.top) + (rule.left < sign.left ? 1000.0f : 0.0f);
    });

    // The contents are whatever was drawn under the bar, other bars included.
    std::vector<Ink> covered;
    for (const Ink& glyph : drawing.glyphs) {
        if (glyph.left >= bar.left - 0.5f && glyph.right <= bar.right + 0.5f) covered.push_back(glyph);
    }
    for (const Ink& rule : drawing.rules) {
        if (rule.top > bar.top + 0.01f && rule.left >= bar.left - 0.5f) covered.push_back(rule);
    }
    if (covered.empty()) return {};
    const Ink contents = around(covered);

    Measure result{
        .found = true,
        .span = sign.bottom - sign.top,
        .pieces = drawing.pieces,
        .meeting = std::abs(sign.top - bar.top),
        .joint = bar.left - sign.right,
        .above = contents.top - bar.bottom,
        .below = sign.bottom - contents.bottom,
        .beside = contents.left - sign.right,
    };
    std::fprintf(stderr,
                 "%-9s %-36s span %6.2f pieces %zu  meets %5.2f  joint %5.2f  above %5.2f  below %5.2f  beside %5.2f\n",
                 displayed ? "display" : "inline", formula.c_str(), result.span, result.pieces, result.meeting,
                 result.joint, result.above, result.below, result.beside);
    return result;
}

int main() {
    const std::filesystem::path assets =
        std::filesystem::path(__FILE__).lexically_normal().parent_path().parent_path().parent_path().parent_path() /
        "assets";

    memory::Arena arena(1u << 24);
    memory::Arena scratch(1u << 22);

    typography::Collection collection(arena);
    const std::size_t surveyed = collection.post((assets / "fonts").string());
    assert((surveyed > 0) && "the font tree is found");
    collection.set("expression", "NewCMMath-Regular");

    typography::Registry registry(arena, collection);
    const typography::Font* font = registry.get({.family = "expression", .size = 12.0f});
    assert((font != nullptr) && "the maths face opens");
    if (!font) return 1;

    const typography::Shaper shaper(arena);
    const layout::Typesetter typesetter(arena, registry, shaper);
    const typography::Expression::Metric table = typography::Expression(*font).metrics();

    const std::vector<std::string> formulas{
        "\\sqrt{x}", "\\sqrt{2}", "\\sqrt{5x}", "\\sqrt{x+1}", "\\sqrt{a^2+b^2}", "\\sqrt{y}",
        "\\sqrt{\\frac{a}{b}}", "\\sqrt{\\frac{1}{2}}", "\\sqrt{\\frac{x^2+1}{y_1}}", "\\sqrt[3]{8}",
        "\\sqrt[n]{x}", "\\sqrt{1+\\sqrt{1+x}}", "\\sqrt{\\pmatrix{a & b \\\\ c & d \\\\ e & f \\\\ g & h}}",
    };

    // Everything any radical must do, in a line and on a line of its own.
    const float least = font->size() / 36.0f - 0.001f;
    for (const bool displayed : {false, true}) {
        const float gap = displayed ? table.clearance : table.gap;
        for (const std::string& formula : formulas) {
            const Measure radical = measure(formula, displayed, *font, typesetter, arena);
            assert((radical.found) && "the radical draws a sign and a bar");
            if (!radical.found) continue;

            assert((radical.meeting < 0.05f) && "the sign's top meets the bar");
            assert((radical.joint > -0.6f && radical.joint < 0.3f) && "the bar starts where the sign ends");
            assert((radical.above >= gap - 0.05f) && "the bar stands the font's gap clear of the contents");
            assert((radical.below >= -0.05f) && "the sign reaches as low as the contents");
            assert((radical.beside >= least) && "the contents stand clear of the sign sideways");
        }
    }

    // A lone letter under a full-size sign sits in the middle of it: the
    // room it has to spare is shared above and below, never all hung below
    // the line -- so there is always at least as much air over the contents
    // as there is sign under them.
    for (const bool displayed : {false, true}) {
        for (const std::string formula : {"\\sqrt{x}", "\\sqrt{2}", "\\sqrt{y}", "\\sqrt{\\frac{a}{b}}"}) {
            const Measure radical = measure(formula, displayed, *font, typesetter, arena);
            assert((radical.above >= radical.below) && "what the sign has to spare is shared, not all hung below");
        }
    }

    // In a line, contents that fill the sign get more than the font's
    // narrowest gap, so a superscript or a fraction never meets the bar.
    for (const std::string formula : {"\\sqrt{a^2+b^2}", "\\sqrt{\\frac{1}{2}}"}) {
        const Measure radical = measure(formula, false, *font, typesetter, arena);
        assert((radical.above >= 2.0f * table.gap) && "a tall term in a line has room above it");
    }

    // The sign grows with what it covers: a fraction takes a taller one than
    // a letter, a displayed fraction a taller one still, and a matrix taller
    // than any single size the font draws is covered by one built of parts.
    {
        const Measure letter = measure("\\sqrt{x}", false, *font, typesetter, arena);
        const Measure fraction = measure("\\sqrt{\\frac{a}{b}}", false, *font, typesetter, arena);
        const Measure shown = measure("\\sqrt{\\frac{a}{b}}", true, *font, typesetter, arena);
        const Measure grid = measure("\\sqrt{\\pmatrix{a & b \\\\ c & d \\\\ e & f \\\\ g & h}}", true, *font,
                                     typesetter, arena);
        assert((fraction.span > letter.span + 1.0f) && "a fraction takes a taller sign than a letter");
        assert((shown.span > fraction.span + 1.0f) && "a displayed fraction takes a taller sign still");
        assert((grid.pieces > 1) && "a matrix's sign is built from the font's parts");
        assert((grid.span > 36.0f) && "a built sign reaches past the tallest size drawn whole");
    }

    // Beyond the radical: a fraction on the maths axis, a display set to the
    // column it stands in, and an inline formula opened into the pieces a
    // line may break between.
    {
        syntax::Lexicon lexicon(arena);
        syntax::semantics::Union state{};
        const syntax::expression::Unicodes unicodes;
        syntax::Mouth mouth(syntax::Cursor(std::vector<syntax::Token>{}), state, lexicon, arena);
        const render::primitives::Expressions expressions(lexicon);
        const auto parse = [&](const std::string& formula, const bool displayed) {
            mouth.ingest(arena.copy(formula + "$"));
            syntax::expression::Parser parser(mouth, unicodes, expressions.grammar, arena,
                                              displayed ? syntax::expression::Node::Style::Display
                                                        : syntax::expression::Node::Style::Inline);
            return parser.parse('$');
        };

        const layout::Node* fraction = typesetter.lower(parse("\\frac{a}{b}", false), *font, 0.0f);
        assert((fraction && fraction->box().height > table.axis && fraction->box().depth > 0.0f) &&
               "a fraction stands on the axis, its numerator above and its denominator below the baseline");

        const layout::Node* shown = typesetter.lower(parse("x = 1", true), *font, 300.0f);
        assert((shown && std::abs(layout::Line::advance(shown) - 300.0f) < 0.5f) &&
               "a display is as wide as its column");

        const memory::Slice<layout::Node*> pieces = typesetter.unfold(parse("a+b=c", false), *font);
        assert((pieces.size() > 1) && "an inline formula opens into pieces a line may break between");

        // A fraction's parts stand clear of its bar as TeX's rule 15 has it:
        // by the font's own least gap -- its bar's thickness in a line, three
        // times that in a display -- whatever hangs from the numerator or
        // rises from the denominator, a descender, a limit, a large operator.
        struct Clear {
            float above{0.0f};   ///< From the numerator's lowest ink down to the bar.
            float below{0.0f};   ///< From the bar down to the denominator's highest ink.
            float least{0.0f};   ///< The smallest glyph's height, top to bottom.
            std::size_t bars{0}; ///< Rules drawn with any width.
            std::size_t glyphs{0};   ///< Glyphs drawn.
        };
        const auto clear = [&](const std::string& formula, const bool displayed) {
            const layout::Node* box = typesetter.lower(parse(formula, displayed), *font, 0.0f);
            assert((box != nullptr) && "the fraction lowers");
            Drawing drawing;
            walk(box, 0.0f, 0.0f, drawing);
            Clear result{.least = std::numeric_limits<float>::max(), .glyphs = drawing.glyphs.size()};
            const Ink* bar = nullptr;
            for (const Ink& rule : drawing.rules) {
                if (rule.right - rule.left <= 0.01f) continue;
                ++result.bars;
                if (!bar || rule.right - rule.left > bar->right - bar->left) bar = &rule;
            }
            for (const Ink& glyph : drawing.glyphs) result.least = std::min(result.least, glyph.bottom - glyph.top);
            if (!bar) return result;
            // A glyph whose middle is over the bar's is the numerator's, one
            // under it the denominator's.
            float lowest = std::numeric_limits<float>::lowest();
            float highest = std::numeric_limits<float>::max();
            const float middle = (bar->top + bar->bottom) * 0.5f;
            for (const Ink& glyph : drawing.glyphs) {
                if ((glyph.top + glyph.bottom) * 0.5f < middle) lowest = std::max(lowest, glyph.bottom);
                else highest = std::min(highest, glyph.top);
            }
            result.above = bar->top - lowest;
            result.below = highest - bar->bottom;
            std::fprintf(stderr, "%-9s %-36s above %5.2f  below %5.2f\n", displayed ? "display" : "inline",
                         formula.c_str(), result.above, result.below);
            return result;
        };
        for (const bool displayed : {false, true}) {
            const float room = displayed ? table.distance : table.separation;
            for (const std::string formula :
                 {"\\frac{p}{q}", "\\frac{g}{h}", "\\frac{f}{g}", "\\frac{x^2}{y_1}", "\\frac{\\partial f}{\\partial x}",
                  "\\frac{\\sqrt{x}}{\\pi}", "\\frac{\\sum_i a_i}{\\prod_j b_j}", "{a \\over b}"}) {
                const Clear measured = clear(formula, displayed);
                assert((measured.bars >= 1) && "a fraction draws its bar");
                assert((measured.above >= room - 0.02f && measured.below >= room - 0.02f) &&
                       "neither part comes nearer its bar than the font's least gap");
            }
        }

        // In a line, a digit over a digit sits where the font's shifts put
        // it, well clear of the bar, not pressed down to the least gap.
        const Clear half = clear("\\frac{1}{2}", false);
        assert((half.above > table.separation + 0.5f && half.below > table.separation + 0.5f) &&
               "the parts of a fraction in a line stand where the font's shifts put them");

        // TeX's own fractions, written between their parts; a stack with no
        // bar; a binomial, fenced; and a fraction in a matrix's cell, which is
        // in text style and so set a size down even in a display.
        assert((clear("{a \\atop b}", true).bars == 0) && "\\atop stacks its parts with no bar");
        assert((clear("{n \\choose k}", false).glyphs == 4) && "\\choose fences its parts in parentheses");
        assert((clear("\\frac{1}{2}", true).least > clear("\\pmatrix{\\frac{1}{2}}", true).least + 1.0f) &&
               "a matrix's cells are in text style, a fraction in one set a size down");
        assert((clear("\\tbinom{n}{k}", true).least + 1.0f < clear("\\dbinom{n}{k}", false).least) &&
               "\\tbinom and \\dbinom say their size whatever surrounds them");
    }

    return 0;
}
