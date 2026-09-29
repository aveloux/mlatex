#pragma once

#include "layout/node.hpp"
#include "memory/arena.hpp"
#include "memory/slice.hpp"

#include <cstdint>

namespace render::layout {

    /// @brief Packs a run of nodes into one box.
    ///
    /// This is TeX's `\\hbox` and `\\vbox`. Both measure their contents and
    /// return a single node standing for the whole run, which is what lets a
    /// line sit inside a paragraph, a paragraph inside a page, and a fraction
    /// inside a line, all measured the same way.
    ///
    /// @par Reference points
    /// Every box has one, and everything that draws or measures a box works
    /// from it: #Node::Box::height is what stands above it and
    /// #Node::Box::depth what hangs below.
    ///
    /// A horizontal box puts its reference point on the baseline, so its
    /// height is the ink above the line and its depth the ink below. A
    /// vertical box puts its reference point on its top edge, so its height is
    /// zero and its depth is the whole run. One consequence worth knowing: a
    /// column dropped into a line of text therefore runs downwards from the
    /// baseline, and is raised onto it by giving it a shift.
    ///
    /// @par Use
    /// @code
    /// // A line set to the column width, stretching its spaces to fit:
    /// layout::Node* line = layout::Line::horizontal(arena, glyphs, column);
    ///
    /// // Those lines stacked, each centred in the column:
    /// layout::Node* block = layout::Line::vertical(arena, lines, leading);
    /// @endcode
    class Line {
    public:
        /// @brief Where a vertical box's contents sit across the column.
        enum class Anchor : std::uint8_t {
            Start,   ///< Flush left, as a page of body text is.
            Middle   ///< Centred, as a fraction's numerator is over its bar.
        };

        /// @brief Packs nodes left to right.
        ///
        /// When a target width is given, the glue inside is stretched or shrunk
        /// to reach it and the ratio is recorded on the box, so that drawing
        /// can apply it without measuring again.
        ///
        /// @param arena  Allocator for the box.
        /// @param nodes  Contents, in reading order; kept by reference.
        /// @param target Width to set to, or 0 to take the natural width.
        /// @return The box; never null.
        /// @complexity O(n) in the nodes.
        [[nodiscard]] static Node* horizontal(
            memory::Arena& arena,
            memory::Slice<Node*> nodes,
            float target = 0.0f
        ) noexcept;

        /// @brief Packs nodes top to bottom.
        ///
        /// @param arena  Allocator for the box and for any leading inserted.
        /// @param nodes   Contents, in reading order.
        /// @param skip    Space to leave between each pair, or 0 for none.
        /// @param anchor  Where the contents sit across the column.
        /// @return The box; never null.
        /// @complexity O(n) in the nodes.
        [[nodiscard]] static Node* vertical(
            memory::Arena& arena,
            memory::Slice<Node*> nodes,
            float skip = 0.0f,
            Anchor anchor = Anchor::Start
        ) noexcept;

        /// @brief How far a node moves the pen along a line.
        /// @param node Node to measure; null measures zero.
        /// @complexity O(1).
        [[nodiscard]] static float advance(const Node* node) noexcept;

        /// @brief How much vertical room a node takes in a column.
        /// @param node Node to measure; null measures zero.
        /// @complexity O(1).
        [[nodiscard]] static float extent(const Node* node) noexcept;

        /// @brief Puts a line's contents in the order they are drawn.
        ///
        /// A line is built in the order its text was written. Where that text
        /// reads right to left -- Arabic, Hebrew -- the order it is drawn in
        /// is another: Unicode's bidirectional algorithm, taken a word at a
        /// time, since a word's own glyphs already come from the shaper in
        /// the order they are drawn. Each word is given its level: one for
        /// right-to-left letters, the paragraph's for anything between two
        /// words that disagree, and two -- keeping its own order inside the
        /// text around it -- for a number, or for Latin letters in a line
        /// that reads right to left. Every run at a level or above is then
        /// turned round, from the highest level down to one. A line with
        /// nothing right to left in it, in a paragraph that reads left to
        /// right, is left as it is.
        ///
        /// @param arena    Allocator for the reordered list.
        /// @param line     A horizontal box; its list is replaced. Anything
        ///                 else, or null, is left as it is.
        /// @param reversed Whether the line reads right to left, as its
        ///                 paragraph does.
        /// @complexity O(n) in the line's nodes.
        static void reorder(memory::Arena& arena, Node* line, bool reversed);
    };

}
