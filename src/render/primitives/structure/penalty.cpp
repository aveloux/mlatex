#include "render/primitives/structure/penalty.hpp"
#include "render/primitives/directive.hpp"

#include "render/layout/node.hpp"
#include "syntax/mouth.hpp"
#include "syntax/node.hpp"

namespace render::primitives::structure {

    void penalties(syntax::Parser& parser, syntax::semantics::Registers& registers) {
        const syntax::Symbol identifier = parser.mouth().lexicon().intern("\\count");

        parser.bind("\\penalty", [&registers, identifier](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth();
            memory::Arena& arena = parser.arena();
            const memory::Location origin = mouth.lookahead().location;
            const auto value = mouth.integer(registers, identifier);

            auto* node = arena.compose<layout::Node>(layout::Node::Type::Penalty);
            node->penalty({ .value = value.value_or(0) });
            return directive(arena, node, origin);
        });

        parser.bind("\\nobreak", [](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena();
            const memory::Location origin = parser.mouth().lookahead().location;

            auto* node = arena.compose<layout::Node>(layout::Node::Type::Penalty);
            node->penalty({ .value = 10000 });
            return directive(arena, node, origin);
        });

        parser.bind("\\break", [](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena();
            const memory::Location origin = parser.mouth().lookahead().location;

            auto* node = arena.compose<layout::Node>(layout::Node::Type::Penalty);
            node->penalty({ .value = -10000 });
            return directive(arena, node, origin);
        });

        parser.bind("\\linebreak", [](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena();
            const memory::Location origin = parser.mouth().lookahead().location;

            auto* node = arena.compose<layout::Node>(layout::Node::Type::Penalty);
            node->penalty({ .value = -10000 });
            return directive(arena, node, origin);
        });

        parser.bind("\\pagebreak", [](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena();
            const memory::Location origin = parser.mouth().lookahead().location;

            auto* node = arena.compose<layout::Node>(layout::Node::Type::Pause);
            node->pause({ .penalty = { .value = -10000 } });
            return directive(arena, node, origin);
        });
    }

}