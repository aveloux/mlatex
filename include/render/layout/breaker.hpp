#pragma once

#include "layout/node.hpp"
#include "memory/arena.hpp"
#include "memory/slice.hpp"

namespace render::layout {

    /// @brief Breaks a run of glyphs into lines, optimally.
    ///
    /// This is Knuth and Plass's algorithm, and what separates it from
    /// breaking greedily is that it chooses every break in a paragraph at
    /// once. A line that would be loose is accepted when it lets the next two
    /// come out even, which is why a paragraph set this way has no lone word
    /// stranded on its last line.
    ///
    /// @par How a break is judged
    /// Each candidate line gets a badness from how far its glue had to stretch
    /// or shrink -- the cube of the ratio, so a slightly loose line is nearly
    /// free and a very loose one is not. Squaring that and adding the penalty
    /// on the break gives its demerits, and the paragraph chosen is the one
    /// whose demerits add to the least.
    ///
    /// @par Alignment
    /// Ragged and centred setting is the same algorithm with infinitely
    /// stretchable glue at one edge of every line, or both, as LaTeX's
    /// `\\raggedright` and `\\centering` have it. A line with glue like that
    /// is never loose, so the breaker is left choosing only how few lines it
    /// can manage -- which is exactly how a ragged paragraph is set. The line
    /// before a forced break, and the last line of a justified paragraph, get
    /// the same treatment at their right edge and are set at their natural
    /// width instead of being stretched across the column.
    ///
    /// @par Cost
    /// Breaks are only considered from candidates that could still reach the
    /// current position, and a candidate too far back to reach it is dropped
    /// for good. The active set therefore stays at the handful of breaks a
    /// line's worth of text admits, which makes the pass linear in the glyphs
    /// rather than quadratic in them.
    ///
    /// @par Use
    /// @code
    /// const layout::Breaker breaker(arena, scratch, {.target = column});
    /// const auto lines = breaker.compose(glyphs);
    /// @endcode
    class Breaker {
    public:
        /// @brief How lines are to be judged.
        struct Configuration {
            float target{400.0f};      ///< Column width in points.
            float pretolerance{100.0f};   ///< Worst badness on the first pass, which hyphenates nothing: `\\pretolerance`.
            float tolerance{200.0f};      ///< Worst badness on the second, which hyphenates: `\\tolerance`.
            float emergency{30.0f};       ///< Stretch every line is granted on a last pass, when neither finds
                                          ///< lines that fit: `\\emergencystretch`, three ems at ten points.
            float penalty{10.0f};      ///< Added to every break, to prefer fewer lines.
            Node::Justification justification{Node::Justification::Full};   ///< How lines sit in the column.
        };

        /// @brief Binds a breaker to its allocators and its rules.
        /// @param arena Allocator for the lines produced.
        /// @param scratch Allocator for the pass's own arrays.
        /// @param configuration How lines are to be judged.
        Breaker(memory::Arena& arena, memory::Arena& scratch, const Configuration& configuration) noexcept;

        /// @brief Breaks a run into lines.
        /// @param input Glyphs, glue and penalties, in reading order.
        /// @return One horizontal box per line, each set to the target width
        ///         except where the alignment sets a line at its natural one.
        /// @complexity O(n) in the input for ordinary text, where only a
        ///             line's worth of breaks is ever active at once.
        [[nodiscard]] memory::Slice<Node*> compose(memory::Slice<Node*> input) const;

    private:
        memory::Arena& arena;      ///< Allocator for the lines.
        memory::Arena& scratch;    ///< Allocator for the pass's arrays.
        Configuration rules{};     ///< How lines are judged.
    };

}
