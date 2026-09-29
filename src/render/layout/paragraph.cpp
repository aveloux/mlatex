/// @file
/// @brief Paragraph implementation: a horizontal list broken into lines.
///
/// The breaker chooses where the lines end; the work here is spacing them
/// afterwards. That is TeX's `\\baselineskip` rule, and doing it per gap
/// rather than uniformly is what keeps a line holding a tall fraction from
/// colliding with the one above it.
#include "layout/paragraph.hpp"
#include "layout/breaker.hpp"
#include "layout/line.hpp"

#include <algorithm>

namespace render::layout {

    Paragraph::Paragraph(
        memory::Arena& arena,
        const memory::Slice<Node*> material,
        const Node::Justification setting,
        const float left,
        const float right,
        const bool reversed
    ) noexcept
        : arena(arena), content(material), justification(setting), margin(left),
          gutter(right), reversed(reversed) {}

    void Paragraph::layout(memory::Arena& scratch, const float width, const float leading) noexcept {
        // Lines already broken to this column stay broken. A column of a
        // different width needs new ones even though the content is the same.
        if (tree && column == width) return;
        column = width;

        if (content.empty() || width - margin - gutter <= 0.0f) {
            tree = nullptr;
            return;
        }

        const Breaker breaker(arena, scratch, Breaker::Configuration{
            .target = width - margin - gutter,
            .penalty = 10.0f,
            .justification = justification
        });

        const memory::Slice<Node*> lines = breaker.compose(content);
        if (lines.empty()) {
            tree = nullptr;
            return;
        }

        // Put in the order they are drawn, wherever there is anything that
        // reads right to left: see the class notes.
        for (Node* line : lines) Line::reorder(arena, line, reversed);

        // One glue between each pair of lines, sized so the baselines end up
        // the asked-for distance apart. A line deep enough to eat that
        // distance gets the minimum clearance instead of a negative gap.
        const memory::Slice<Node*> rows = arena.allocate<Node*>(lines.count * 2 - 1);
        std::size_t filled = 0;
        float depth = 0.0f;

        for (std::size_t index = 0; index < lines.count; ++index) {
            Node* line = lines[index];
            if (!line) continue;

            if (filled > 0) {
                auto* glue = arena.compose<Node>(Node::Type::Glue);
                glue->glue({.width = std::max(leading - depth - line->box().height,
                                              leading * minimum)});
                rows[filled++] = glue;
            }

            // Moved in by the margin, which is how every line of a list item
            // lines up under the first word after its label -- the margin at
            // the right, right to left, where a label then hangs.
            if (const float inset = reversed ? gutter : margin; inset != 0.0f) {
                Node::Box shape = line->box();
                shape.offset = inset;
                line->box(shape);
            }

            rows[filled++] = line;
            depth = line->box().depth;
        }

        tree = Line::vertical(arena, memory::Slice{rows.data, filled}, 0.0f);
    }

}
