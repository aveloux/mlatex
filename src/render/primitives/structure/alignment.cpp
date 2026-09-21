#include "render/primitives/structure/alignment.hpp"
#include "render/primitives/structure/content.hpp"
#include "render/primitives/directive.hpp"

#include "render/layout/line.hpp"
#include "syntax/mouth.hpp"
#include "syntax/node.hpp"
#include "syntax/semantics/scope.hpp"
#include "syntax/semantics/union.hpp"

#include <algorithm>
#include <span>
#include <vector>

namespace render::primitives::structure {

    static layout::Node* cell(
        syntax::Parser& parser,
        const typography::Shaper& shaper,
        const layout::Typesetter& typesetter,
        const configuration::Selection& selection
    ) {
        memory::Arena& arena = parser.arena();
        const memory::Slice<syntax::Node*> children = parser.parse('}');

        std::vector<layout::Node*> nodes;
        nodes.reserve(children.size());
        for (std::size_t index = 0; index < children.size(); ++index) {
            append(nodes, children[index], shaper, selection.font(), typesetter);
        }

        memory::Slice<layout::Node*> slice = arena.allocate<layout::Node*>(nodes.size());
        for (std::size_t index = 0; index < nodes.size(); ++index) {
            slice[index] = nodes[index];
        }

        return layout::Line::horizontal(arena, slice, 0.0f);
    }

    void alignment(
        syntax::Parser& parser,
        const typography::Shaper& shaper,
        const layout::Typesetter& typesetter,
        const configuration::Selection& selection
    ) {
        parser.bind("\\halign", [&shaper, &typesetter, &selection](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth();
            memory::Arena& arena = parser.arena();
            const memory::Location origin = mouth.lookahead().location;

            const syntax::Symbol row = mouth.lexicon().intern("\\cr");
            const syntax::Symbol column = mouth.lexicon().intern("&");

            syntax::Token open = mouth.read();
            if (open.values != "{" && !open.empty()) {
                mouth.inject(std::span{&open, 1});
            }

            mouth.state().scope().push(syntax::semantics::Scope::Type::Alignment);

            std::vector<std::vector<layout::Node*>> grid;
            std::vector<layout::Node*> line;

            while (true) {
                syntax::Token brace = mouth.read();
                if (brace.values != "{" && !brace.empty()) {
                    mouth.inject(std::span{&brace, 1});
                }

                line.push_back(cell(parser, shaper, typesetter, selection));

                const syntax::Token next = mouth.read();
                if (next.symbol == column) {
                    continue;
                }

                if (next.symbol == row) {
                    grid.push_back(std::move(line));
                    line.clear();

                    const syntax::Token close = mouth.read();
                    if (close.values == "}" || close.empty()) break;
                    mouth.inject(std::span{&close, 1});
                    continue;
                }

                break;
            }

            mouth.state().scope().pop();

            std::size_t columns = 0;
            for (const auto& entry : grid) {
                columns = std::max(columns, entry.size());
            }

            memory::Slice<float> widths = arena.allocate<float>(columns);
            for (std::size_t index = 0; index < columns; ++index) widths[index] = 0.0f;

            for (const auto& entry : grid) {
                for (std::size_t index = 0; index < entry.size(); ++index) {
                    if (const layout::Node* box = entry[index]) {
                        widths[index] = std::max(widths[index], box->box().width);
                    }
                }
            }

            memory::Slice<layout::Node*> rows = arena.allocate<layout::Node*>(grid.size());

            for (std::size_t outer = 0; outer < grid.size(); ++outer) {
                const auto& entry = grid[outer];

                std::size_t total = entry.size();
                for (std::size_t index = 0; index < entry.size(); ++index) {
                    const float span = entry[index] ? entry[index]->box().width : 0.0f;
                    if (span < widths[index]) total++;
                }

                memory::Slice<layout::Node*> flat = arena.allocate<layout::Node*>(total);
                std::size_t mark = 0;

                for (std::size_t index = 0; index < entry.size(); ++index) {
                    layout::Node* box = entry[index];
                    flat[mark++] = box;

                    const float span = box ? box->box().width : 0.0f;
                    if (const float gap = widths[index] - span; gap > 0.0f) {
                        auto* pad = arena.compose<layout::Node>(layout::Node::Type::Kern);
                        pad->kern({ .width = gap });
                        flat[mark++] = pad;
                    }
                }

                rows[outer] = layout::Line::horizontal(arena, flat, 0.0f);
            }

            layout::Node* table = layout::Line::vertical(arena, rows, 0.0f);
            return directive(arena, table, origin);
        });
    }

}