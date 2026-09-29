#pragma once

#include "layout/node.hpp"
#include "memory/arena.hpp"
#include "memory/slice.hpp"

namespace render::layout {

    /// @brief One paragraph: a horizontal list, and the lines it breaks into.
    ///
    /// The list is everything the paragraph is made of, already measured --
    /// glyphs, the glue between words, an inline formula, a box a primitive
    /// built. A paragraph does not know which of those it holds and does not
    /// need to; they all measure the same way, which is what lets a fraction
    /// sit in a line of prose and be broken around like any other word.
    ///
    /// @par Alignment and margin
    /// A paragraph keeps the justification and the margins that were in force
    /// when it ended, as TeX does: `\\centering` part way through a paragraph
    /// centres all of it. The left margin is how a list item's lines all start
    /// at the same indent -- the column narrows by it, and each line is moved
    /// in by it -- while the label, which starts the first line with a
    /// negative kern, hangs out into the space it leaves. The right margin
    /// narrows the column the same way without moving anything, which is what
    /// `quote` and `verse` set on both sides at once to indent a passage in
    /// from either edge.
    ///
    /// @par Direction
    /// A paragraph in Arabic or Hebrew reads right to left: its lines start
    /// at the right, its first line's indent is at the right, and a list
    /// item's margin is taken from the right. Its words, and a run of words
    /// in such a script inside a paragraph that reads left to right, are put
    /// in the order they are drawn line by line, once the lines are broken
    /// in the order they were written: Unicode's bidirectional algorithm,
    /// taken a word at a time. A word's own glyphs already are in that
    /// order, as the shaper hands them over; a number, or a phrase in Latin
    /// letters, keeps its own order inside the text around it.
    ///
    /// @par Use
    /// @code
    /// layout::Paragraph paragraph(arena, content, Node::Justification::Full, 0.0f, 0.0f);
    /// paragraph.layout(scratch, column, leading);
    ///
    /// layout::Node* lines = paragraph.node();
    /// @endcode
    class Paragraph {
    public:
        /// @brief Builds a paragraph over a horizontal list.
        /// @param arena       Allocator for the lines produced.
        /// @param material      What the paragraph is made of; kept by reference.
        /// @param setting       How its lines sit in the column.
        /// @param left          How far in from the column's left edge every line starts.
        /// @param right         How far in from the column's right edge every line ends.
        /// @param reversed      Whether it reads right to left.
        Paragraph(memory::Arena& arena, memory::Slice<Node*> material,
                  Node::Justification setting, float left, float right = 0.0f, bool reversed = false) noexcept;

        /// @brief Breaks the paragraph into lines.
        ///
        /// Laying out again against the same column costs nothing, so a second
        /// pass over a document is free.
        ///
        /// @param scratch Allocator for the breaker's own arrays.
        /// @param width   Column width in points.
        /// @param leading Baseline-to-baseline distance in points.
        /// @complexity O(n) in the list for ordinary prose.
        void layout(memory::Arena& scratch, float width, float leading) noexcept;

        /// @brief The vertical box of lines, or nullptr before layout.
        [[nodiscard]] Node* node() const noexcept { return tree; }

        /// Least space left between two lines that would otherwise touch, as a
        /// fraction of the asked-for leading. This is TeX's `\\lineskip`: when
        /// two lines are already closer than the baseline distance -- a tall
        /// fraction over an ordinary line -- they are separated by this much
        /// rather than pulled together.
        static constexpr float minimum = 0.1f;

    private:
        memory::Arena& arena;                ///< Allocator for the lines.
        memory::Slice<Node*> content{};      ///< What the paragraph is made of.
        Node::Justification justification{Node::Justification::Full};   ///< How its lines sit.
        float margin{0.0f};                  ///< Indent of every line from the column's left edge.
        float gutter{0.0f};                  ///< Indent of every line from the column's right edge.
        bool reversed{false};                ///< Whether it reads right to left.
        float column{0.0f};                  ///< Width it was last broken to.
        Node* tree{nullptr};                 ///< Vertical box of the lines.
    };

}
