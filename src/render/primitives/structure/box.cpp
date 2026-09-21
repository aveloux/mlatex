#include "render/primitives/structure/box.hpp"
#include "render/primitives/structure/content.hpp"
#include "render/primitives/directive.hpp"

#include "render/layout/line.hpp"
#include "syntax/mouth.hpp"
#include "syntax/node.hpp"
#include "syntax/number.hpp"
#include "syntax/semantics/scope.hpp"
#include "syntax/semantics/union.hpp"

#include <span>
#include <vector>

namespace render::primitives::structure {

    static syntax::Node* compose(
        syntax::Parser& parser,
        const syntax::semantics::Registers& registers,
        const typography::Shaper& shaper,
        const layout::Typesetter& typesetter,
        const configuration::Selection& selection,
        const bool horizontal,
        const bool bounded
    ) {
        syntax::Mouth& mouth = parser.mouth();
        memory::Arena& arena = parser.arena();

        float target = 0.0f;
        bool specified = false;

        if (bounded) {
            syntax::Token to = mouth.read();
            if (to.values == "to") {
                const syntax::Symbol identifier = mouth.lexicon().intern("\\count");
                const syntax::Symbol dimension = mouth.lexicon().intern("\\dimen");
                if (const auto value = mouth.dimension(registers, identifier, dimension)) {
                    target = static_cast<float>(*value) / static_cast<float>(syntax::Number::scale);
                    specified = true;
                }
            } else if (!to.empty()) {
                mouth.inject(std::span{&to, 1});
            }
        }

        syntax::Token open = mouth.read();
        if (open.values != "{" && !open.empty()) {
            mouth.inject(std::span{&open, 1});
        }

        const memory::Location origin = mouth.lookahead().location;

        mouth.state().scope().push(syntax::semantics::Scope::Type::Box);
        const memory::Slice<syntax::Node*> children = parser.parse('}');
        mouth.state().scope().pop();

        std::vector<layout::Node*> nodes;
        nodes.reserve(children.size());

        for (std::size_t index = 0; index < children.size(); ++index) {
            append(nodes, children[index], shaper, selection.font(), typesetter);
        }

        memory::Slice<layout::Node*> slice = arena.allocate<layout::Node*>(nodes.size());
        for (std::size_t index = 0; index < nodes.size(); ++index) {
            slice[index] = nodes[index];
        }

        layout::Node* box = horizontal
            ? layout::Line::horizontal(arena, slice, specified ? target : 0.0f)
            : layout::Line::vertical(arena, slice, 0.0f);

        return directive(arena, box, origin);
    }

    void boxes(
        syntax::Parser& parser,
        syntax::semantics::Registers& registers,
        const typography::Shaper& shaper,
        const layout::Typesetter& typesetter,
        const configuration::Selection& selection
    ) {
        parser.bind("\\hbox", [&registers, &shaper, &typesetter, &selection](syntax::Parser& parser) -> syntax::Node* {
            return compose(parser, registers, shaper, typesetter, selection, true, true);
        });

        parser.bind("\\vbox", [&registers, &shaper, &typesetter, &selection](syntax::Parser& parser) -> syntax::Node* {
            return compose(parser, registers, shaper, typesetter, selection, false, false);
        });

        parser.bind("\\vtop", [&registers, &shaper, &typesetter, &selection](syntax::Parser& parser) -> syntax::Node* {
            return compose(parser, registers, shaper, typesetter, selection, false, false);
        });
    }

}