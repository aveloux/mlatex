#include "latex.hpp"
#include "layout/document.hpp"
#include "layout/typesetter.hpp"
#include "memory/arena.hpp"
#include "typography/collection.hpp"
#include "typography/registry.hpp"
#include "typography/shaper.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iterator>

// The document: material in the order it was written, gathered into
// paragraphs and the blocks between them -- a word space never lost to the
// cache of runs already shaped, a paragraph's first line indented as
// LaTeX's are, and each paragraph broken to the columns it was written in.

/// The engine's assets directory, found from this file.
static std::filesystem::path assets() {
    return latex::locate(__FILE__);
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

namespace layout = render::layout;

int main() {
    Fonts fonts;
    if (!fonts.text) return 0;
    const layout::Typesetter typesetter(fonts.arena, fonts.registry, fonts.shaper);
    const render::typography::Font& font = *fonts.text;

    // --- Paragraphs and blocks -----------------------------------------------------
    {
        layout::Document document(fonts.arena, fonts.scratch, fonts.shaper, typesetter);
        document.append("First paragraph.", font, 10.0f);
        document.separate();
        document.separate();
        document.append("Second.", font, 10.0f);
        auto* table = fonts.arena.compose<layout::Node>(layout::Node::Type::Box);
        table->box({.width = 50.0f, .height = 10.0f});
        document.append(table, true);
        document.append("Third.", font, 10.0f);
        document.layout();
        const memory::Slice<layout::Document::Element*> elements = document.elements();
        assert((elements.size() == 4) && "two paragraphs, a block that ends the second, and the text after it");
        assert((elements[2]->type == layout::Document::Element::Type::Directive) && "the block stands on its own");

        const auto opening = [](const layout::Document::Element* element) {
            const layout::Node* line = element->paragraph->node()->box().list[0];
            return line->box().list[0];
        };
        assert((opening(elements[0])->type == layout::Node::Type::Kern) && "a paragraph opens indented");
        assert((opening(elements[3])->type != layout::Node::Type::Kern) &&
               "the text carrying straight on from a table does not");
    }

    // --- A word space is not lost to a cached run ----------------------------------
    {
        layout::Document document(fonts.arena, fonts.scratch, fonts.shaper, typesetter);
        // A run that opens a paragraph -- as the text after a display does,
        // the line's end still in front of it -- and the same run part way
        // through another, after other material.
        document.append("\nfor every ", font, 10.0f);
        document.separate();
        document.append("a", font, 10.0f);
        document.append(" for every ", font, 10.0f);
        document.layout();

        const memory::Slice<layout::Document::Element*> elements = document.elements();
        assert((elements.size() == 2) && "two paragraphs");
        if (elements.size() == 2 && elements[1]->paragraph && elements[1]->paragraph->node()) {
            const memory::Slice<layout::Node*>& row = elements[1]->paragraph->node()->box().list[0]->box().list;
            const auto a = std::ranges::find_if(row, [](const layout::Node* node) {
                return node && node->type == layout::Node::Type::Glyph && node->glyph().point == 'a';
            });
            assert((a != row.end() && std::next(a) != row.end() &&
                   (*std::next(a))->type == layout::Node::Type::Glue) &&
                   "the space before a run seen before at a paragraph's head is kept");
        }
    }

    // --- Columns ----------------------------------------------------------------------
    {
        layout::Document document(fonts.arena, fonts.scratch, fonts.shaper, typesetter);
        document.configuration.columns = 2;
        document.configuration.gap = 10.0f;
        const layout::Document::Configuration& page = document.configuration;
        const float text = page.width - page.left - page.right;
        assert((std::abs(document.column() - (text - 10.0f) / 2.0f) < 0.01f) &&
               "the class's two columns share the text block");

        document.append("In two columns.", font, 10.0f);
        auto* spread = fonts.arena.compose<layout::Node>(layout::Node::Type::Directive);
        spread->directive({.command = layout::Node::Directive::Command::Columns, .index = 1});
        document.append(spread, true);
        assert((document.column() == text) && "a change of columns changes the width from there on");
        document.append("Across.", font, 10.0f);
        document.layout();

        const memory::Slice<layout::Document::Element*> elements = document.elements();
        assert((elements.size() == 3 && elements[0]->columns == 2 && elements[2]->columns == 1) &&
               "each paragraph keeps the columns it was written in");
        const float narrow = elements[0]->paragraph->node()->box().list[0]->box().width;
        const float wide = elements[2]->paragraph->node()->box().list[0]->box().width;
        assert((std::abs(narrow - document.column(2)) < 0.01f && std::abs(wide - text) < 0.01f) &&
               "and is broken to their width");
    }

    // --- Languages -----------------------------------------------------------------
    {
        const render::typography::Shaper covering(fonts.arena, &fonts.registry);
        layout::Document document(fonts.arena, fonts.scratch, covering, typesetter);
        const render::typography::Hyphenator russian;
        assert((russian.compose((assets() / "hyphens" / "hyph-ru.pat.txt").string()) > 0) &&
               "the Russian patterns read");
        const auto flagged = [](const memory::Slice<layout::Node*> nodes) {
            return std::ranges::count_if(nodes, [](const layout::Node* node) {
                return node->type == layout::Node::Type::Penalty && node->penalty().flag;
            });
        };
        const auto order = [&fonts](const layout::Node::Directive& said) {
            auto* node = fonts.arena.compose<layout::Node>(layout::Node::Type::Directive);
            node->directive(said);
            return node;
        };

        assert((flagged(document.set("электрификация", font, true)) == 0) &&
               "with no language chosen, no word is broken");
        document.append(order({.command = layout::Node::Directive::Command::Language, .hyphenator = &russian,
                               .before = 2, .after = 2}), false);
        assert((flagged(document.set("ЭЛЕКТРИФИКАЦИЯ", font, true)) >= 3) &&
               "a language chosen breaks the words after it by its patterns, capitals folded first");
        assert((flagged(document.set("дом", font, true)) == 0) &&
               "no nearer a word's ends than the language allows");
        assert((flagged(document.set("(ЭЛЕКТРИФИКАЦИЯ).", font, true)) >= 3) &&
               "a word's brackets and stop stand aside from it");
        assert((flagged(document.set("дом/электрификация", font, true)) == 0) &&
               "only the first run of letters is a word, as TeX's is: a path breaks nowhere");

        const memory::Slice<layout::Node*> opened = document.set("  два слова", font, true);
        assert((opened[0]->type == layout::Node::Type::Glyph &&
                std::ranges::count_if(opened, [](const layout::Node* node) {
                    return node->type == layout::Node::Type::Glue;
                }) == 1) && "a run set on its own drops a blank at the head of what it opens, and keeps a word space");
        assert((document.set(" слово", font, false)[0]->type == layout::Node::Type::Glue) &&
               "and keeps a blank where it opens nothing");

        // Right to left: the paragraph's last line is filled from its left,
        // where its end is drawn.
        document.append(order({.command = layout::Node::Directive::Command::Direction, .reversed = true}), false);
        document.append("слово", font, 10.0f);
        document.layout();
        const memory::Slice<layout::Document::Element*> elements = document.elements();
        const layout::Node* line = elements[elements.size() - 1]->paragraph->node()->box().list[0];
        assert((line->box().list[0]->type == layout::Node::Type::Glue) &&
               "a paragraph read right to left ends at the left of its last line");
    }

    return 0;
}
