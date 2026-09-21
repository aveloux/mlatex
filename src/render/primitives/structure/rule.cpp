#include "render/primitives/structure/rule.hpp"
#include "render/primitives/directive.hpp"

#include "render/layout/node.hpp"
#include "syntax/mouth.hpp"
#include "syntax/node.hpp"
#include "syntax/number.hpp"

namespace render::primitives::structure {

    void rules(syntax::Parser& parser, syntax::semantics::Registers& registers) {
        const syntax::Symbol identifier = parser.mouth().lexicon().intern("\\count");
        const syntax::Symbol dimension = parser.mouth().lexicon().intern("\\dimen");

        parser.bind("\\rule", [&registers, identifier, dimension](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth();
            memory::Arena& arena = parser.arena();
            const memory::Location origin = mouth.lookahead().location;

            mouth.read();
            const auto width = mouth.dimension(registers, identifier, dimension);
            mouth.read();

            mouth.read();
            const auto height = mouth.dimension(registers, identifier, dimension);
            mouth.read();

            auto* node = arena.compose<layout::Node>(layout::Node::Type::Rule);
            node->rule({
                .width = width ? static_cast<float>(*width) / static_cast<float>(syntax::Number::scale) : 0.0f,
                .height = height ? static_cast<float>(*height) / static_cast<float>(syntax::Number::scale) : 0.0f
            });
            return directive(arena, node, origin);
        });

        parser.bind("\\hrule", [](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena();
            const memory::Location origin = parser.mouth().lookahead().location;

            auto* node = arena.compose<layout::Node>(layout::Node::Type::Rule);
            node->rule({ .height = 0.4f });
            return directive(arena, node, origin);
        });

        parser.bind("\\vrule", [](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena();
            const memory::Location origin = parser.mouth().lookahead().location;

            auto* node = arena.compose<layout::Node>(layout::Node::Type::Rule);
            node->rule({ .width = 0.4f });
            return directive(arena, node, origin);
        });
    }

}