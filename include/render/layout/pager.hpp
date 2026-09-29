#pragma once

#include "layout/node.hpp"
#include "memory/arena.hpp"
#include "memory/slice.hpp"

#include <cstdint>

namespace render::layout {

    /// @brief Cuts a column of blocks into pages.
    ///
    /// The column arrives as one vertical box holding every block in the
    /// document. This walks it, keeping a running height, and starts a new
    /// page whenever the next block would not fit on the current one.
    ///
    /// @par Footnotes
    /// A block that calls a footnote carries it, as a directive beside the
    /// call's mark. The note's height is reserved at the foot of the page
    /// the block lands on -- with the space and rule above a page's first --
    /// so the block and its note move to the next page together when the
    /// two do not fit, and every note is set on the page that calls it.
    ///
    /// @par Floats
    /// The blocks between a Hold and its Release are a float's, and go
    /// together, placed as LaTeX's output routine places them. Where it is
    /// written, a float goes -- as its `[htbp!]` allows, and nothing of its
    /// kind waits before it -- in the text there (`h`), at the head of the
    /// column (`t`), or at its foot (`b`), each only while the column keeps
    /// the share of text, and the head and foot the shares and counts of
    /// floats, that Placement allows; otherwise it waits. A column that opens
    /// takes a page of waiting floats when they fill enough of one (`p`),
    /// else takes what it can at its head and foot. Floats of one kind keep
    /// their order; one set across the columns of a page of several --
    /// `figure*` -- waits for the head of the next page. A forced page
    /// break, a Barrier and the document's end set every float still
    /// waiting, on pages of their own. One marked fixed, `[H]`, is set where
    /// it stands, on a new page if this one has no room for it.
    ///
    /// @par What it does not do
    /// A block is never split across a page. A paragraph too tall for a page
    /// therefore overhangs it rather than being broken mid-line, which is the
    /// honest outcome until vertical breaking inside a block exists -- the
    /// alternative is to silently lose the overflow.
    ///
    /// @par Use
    /// @code
    /// const layout::Pager pager(arena);
    /// for (const auto& page : pager.paginate(column, {.height = height})) {
    ///     composer.draw(page.nodes, page.notes, left, top);
    /// }
    /// @endcode
    class Pager {
    public:
        /// @brief How LaTeX places floats: the share of a column each area
        ///        may take, how many floats each may hold, and the space
        ///        around them -- the article class's values at ten points,
        ///        until a document sets its own.
        struct Placement {
            float top{0.7f};         ///< `\\topfraction`: the most of a column its head's floats take.
            float bottom{0.3f};      ///< `\\bottomfraction`: the most its foot's take.
            float text{0.2f};        ///< `\\textfraction`: the least of a column with floats left to text.
            float page{0.5f};        ///< `\\floatpagefraction`: the least a page of floats alone holds.
            float spanning{0.7f};    ///< `\\dbltopfraction`: the most of a page its floats across the
                                     ///< columns take.
            float sheet{0.5f};       ///< `\\dblfloatpagefraction`: the least a page of those alone holds.
            std::size_t heads{2};    ///< `topnumber`: floats at a column's head.
            std::size_t feet{1};     ///< `bottomnumber`: at its foot.
            std::size_t most{3};     ///< `totalnumber`: in a column, wherever they stand.
            std::size_t spans{2};    ///< `dbltopnumber`: across the columns at a page's head.
            float apart{12.0f};      ///< `\\floatsep`: between two floats.
            float clear{20.0f};      ///< `\\textfloatsep`: between the floats and the text.
            float amid{12.0f};       ///< `\\intextsep`: above and below a float set in the text.
        };

        /// @brief How much room a page has, and what its footnotes and floats take.
        struct Context {
            float height{792.0f};      ///< Usable height in points, margins already removed.
            float separation{12.0f};   ///< Above a page's first footnote: `\\skip\\footins`, the rule's room included.
            float between{2.0f};       ///< Between one footnote and the next.
            float width{468.0f};       ///< The text block's width, which columns share.
            float gap{10.0f};          ///< Between two columns: `\\columnsep`.
            std::size_t columns{1};    ///< How many the first page starts in: 2 for `[twocolumn]`.
            Placement placement{};     ///< Where floats may go.
        };

        /// @brief One page: the blocks that fall on it.
        struct Page {
            memory::Slice<Node*> nodes{};   ///< Its blocks, in reading order.
            memory::Slice<const Node*> notes{};   ///< The footnotes its blocks call, in order, each a column.
            float height{0.0f};             ///< How much of the page they fill, in points.
            std::int32_t index{0};          ///< Its number, counting from zero.
            std::int32_t badness{0};        ///< How much room it leaves empty, 0 to 10000.
        };

        /// @brief Binds a pager to an allocator.
        /// @param arena Allocator for the pages produced.
        explicit Pager(memory::Arena& arena) noexcept;

        /// @brief Cuts a column into pages.
        /// @param head    The column; anything that is not a vertical box
        ///                yields no pages.
        /// @param context How much room a page has.
        /// @return The pages, in order; never empty for a non-empty column.
        /// @complexity O(n) in the column's blocks.
        [[nodiscard]] memory::Slice<Page> paginate(const Node* head, const Context& context) const;

    private:
        memory::Arena& arena;   ///< Allocator for the pages.
    };

}
