#include "layout/line.hpp"
#include "layout/pager.hpp"
#include "memory/arena.hpp"

#include <cassert>
#include <initializer_list>
#include <vector>

// The pager: a column of blocks cut into pages -- a block that does not fit
// starting the next, a forced break obeyed, floats placed as LaTeX places
// them (at a column's head or foot, in the text when `h` allows, on a page of
// floats alone, a kind keeping its order, a barrier setting what waits), and
// [H] set where written. Pages of several columns are filled a column at a
// time, and a region of columns that ends part way down a page is balanced.

namespace layout = render::layout;
using Command = layout::Node::Directive::Command;

/// @brief The column a document's blocks make, built up block by block.
struct Column {
    memory::Arena& arena;
    std::vector<layout::Node*> blocks{};

    /// @brief A block so tall.
    Column& block(const float height) {
        auto* node = arena.compose<layout::Node>(layout::Node::Type::Box);
        node->box({.width = 100.0f, .height = height});
        blocks.push_back(node);
        return *this;
    }

    /// @brief An instruction of no height.
    Column& order(const layout::Node::Directive& directive) {
        auto* node = arena.compose<layout::Node>(layout::Node::Type::Directive);
        node->directive(directive);
        blocks.push_back(node);
        return *this;
    }

    /// @brief A page break: \\newpage, or \\clearpage for the whole sheet.
    Column& pause(const bool sheet = false) {
        auto* node = arena.compose<layout::Node>(layout::Node::Type::Pause);
        node->pause({.penalty = {.value = -10000, .flag = sheet}});
        blocks.push_back(node);
        return *this;
    }

    /// @brief The pages, at so many columns a page.
    memory::Slice<layout::Pager::Page> paginate(const std::size_t columns = 1) const {
        const memory::Slice<layout::Node*> list = arena.allocate<layout::Node*>(blocks.size());
        for (std::size_t index = 0; index < blocks.size(); ++index) list[index] = blocks[index];
        const layout::Pager pager(arena);
        return pager.paginate(layout::Line::vertical(arena, list, 0.0f),
                              {.height = 250.0f, .width = 210.0f, .gap = 10.0f, .columns = columns});
    }
};

/// @brief How many of a page's top-level nodes are boxes.
static std::size_t boxes(const layout::Pager::Page& page) {
    std::size_t found = 0;
    for (const layout::Node* node : page.nodes) found += node->type() == layout::Node::Type::Box;
    return found;
}

int main() {
    memory::Arena arena(1u << 20);

    // --- One column ------------------------------------------------------------------
    {
        const auto pages = Column{arena}.block(100).block(100).block(100).paginate();
        assert((pages.size() == 2 && boxes(pages[0]) == 2 && boxes(pages[1]) == 1) &&
               "a block that does not fit starts the next page");
    }
    {
        const auto pages = Column{arena}.block(50).pause().block(50).paginate();
        assert((pages.size() == 2) && "a forced break ends the page whatever room is left");
        assert((Column{arena}.block(50).pause().pause().block(50).paginate().size() == 2) &&
               "two breaks in a row make no empty page");
    }
    {
        // A float too tall for what is left waits for the next page's head,
        // and the text after it fills the room it left.
        Column column{arena};
        column.block(150).order({.command = Command::Hold}).block(200).order({.command = Command::Release}).block(50);
        const auto pages = column.paginate();
        assert((pages.size() == 2 && boxes(pages[0]) == 2 && boxes(pages[1]) == 1) &&
               "a float that does not fit moves whole to the next page, the text after it staying");
    }
    {
        Column column{arena};
        column.block(150).order({.command = Command::Hold, .fixed = true}).block(180)
            .order({.command = Command::Release}).block(50);
        const auto pages = column.paginate();
        assert((pages.size() == 2 && boxes(pages[0]) == 1 && boxes(pages[1]) == 2) && "[H] is set where written");
    }

    // --- Floats as LaTeX places them ----------------------------------------------------
    using Directive = layout::Node::Directive;
    const auto height = [](const layout::Node* node) { return layout::Line::extent(node); };
    {
        // Written after text on a page with room, a float goes to its head.
        Column column{arena};
        column.block(50).order({.command = Command::Hold, .place = Directive::Top}).block(60)
            .order({.command = Command::Release}).block(50);
        const auto pages = column.paginate();
        assert((pages.size() == 1 && pages[0].nodes.size() >= 4) && "one page");
        const layout::Node* first = nullptr;
        for (const layout::Node* node : pages[0].nodes) {
            if (node->type() == layout::Node::Type::Box) {
                first = node;
                break;
            }
        }
        assert((first && height(first) == 60.0f) && "a float allowed the head goes above the text written before it");
        assert((pages[0].height == 60.0f + 20.0f + 100.0f) && "\\textfloatsep between it and the text");
    }
    {
        // Asked for the foot, it stands at the foot, below the text after it.
        Column column{arena};
        column.block(50).order({.command = Command::Hold, .place = Directive::Bottom}).block(40)
            .order({.command = Command::Release}).block(50);
        const auto pages = column.paginate();
        const layout::Node* last = nullptr;
        for (const layout::Node* node : pages[0].nodes) {
            if (node->type() == layout::Node::Type::Box) last = node;
        }
        assert((pages.size() == 1 && last && height(last) == 40.0f) && "a float asked for the foot is set last");
        assert((pages[0].height == 250.0f) && "at the very foot, what the text leaves between them");
    }
    {
        // Two floats that fill enough of a page between them go on a page
        // of their own, and the text goes on filling the page before.
        Column column{arena};
        column.block(150);
        for (int index = 0; index < 2; ++index) {
            column.order({.command = Command::Hold}).block(100).order({.command = Command::Release});
        }
        column.block(100);
        const auto pages = column.paginate();
        assert((pages.size() == 2 && boxes(pages[0]) == 2 && boxes(pages[1]) == 2) &&
               "a page of floats alone, when they fill more than \\floatpagefraction of it");
    }
    {
        // A float that waits holds back the next of its kind, even one that
        // would fit; one of another kind may pass.
        Column column{arena};
        column.block(150);
        column.order({.command = Command::Hold, .place = Directive::Top, .index = 0}).block(120)
            .order({.command = Command::Release});
        column.order({.command = Command::Hold, .place = Directive::Here, .index = 0}).block(20)
            .order({.command = Command::Release});
        column.order({.command = Command::Hold, .place = Directive::Here, .index = 1}).block(10)
            .order({.command = Command::Release});
        const auto pages = column.paginate();
        assert((!pages.empty() && boxes(pages[0]) == 2) &&
               "the second figure waits behind the first; the table goes where it is written");
    }
    {
        // A barrier sets what waits before the text after it.
        Column column{arena};
        column.block(200).order({.command = Command::Hold, .place = Directive::Top}).block(100)
            .order({.command = Command::Release});
        column.order({.command = Command::Barrier}).block(30);
        const auto pages = column.paginate();
        assert((pages.size() == 3 && boxes(pages[1]) == 1 && boxes(pages[2]) == 1) &&
               "the float on a page of its own, the text after the barrier on the next");
    }

    // --- Two columns --------------------------------------------------------------------
    {
        const auto pages = Column{arena}.block(100).block(100).block(100).block(100).block(100).paginate(2);
        assert((pages.size() == 2) && "a page of two columns takes four blocks where one takes two");
        assert((!pages.empty() && boxes(pages[0]) == 1 &&
               pages[0].nodes[pages[0].nodes.size() - 1]->box().list.size() == 3) &&
               "the two set side by side, the gap between them");
    }
    {
        const auto pages = Column{arena}.block(100).pause().block(100).paginate(2);
        assert((pages.size() == 1) && "\\newpage ends the column, not the page");
        assert((Column{arena}.block(100).pause(true).block(100).paginate(2).size() == 2) &&
               "\\clearpage ends the page");
    }
    {
        // A region of two columns ending part way down: balanced, and what
        // follows set across beneath it.
        Column column{arena};
        column.block(40).order({.command = Command::Columns, .index = 2});
        column.block(30).block(30).block(30).block(30);
        column.order({.command = Command::Columns, .index = 1}).block(40);
        const auto pages = column.paginate();
        assert((pages.size() == 1) && "a balanced region leaves room below it");
        if (pages.size() == 1) {
            const layout::Node* region = nullptr;
            for (const layout::Node* node : pages[0].nodes) {
                if (node->type() == layout::Node::Type::Box && node->box().list.size() == 3) region = node;
            }
            assert((region && layout::Line::extent(region) == 60.0f) && "its four blocks two to a column, level");
            assert((pages[0].height == 140.0f) && "the page is what stands above, the region, and what follows");
        }
    }

    return 0;
}
