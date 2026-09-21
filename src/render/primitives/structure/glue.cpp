#include "render/primitives/structure/glue.hpp"
#include "render/primitives/directive.hpp"

#include "render/layout/node.hpp"
#include "syntax/mouth.hpp"
#include "syntax/node.hpp"
#include "syntax/number.hpp"

namespace render::primitives::structure {

    void glue(syntax::Parser& parser, syntax::semantics::Registers& registers) {
        const syntax::Symbol identifier = parser.mouth().lexicon().intern("\\count");
        const syntax::Symbol dimension = parser.mouth().lexicon().intern("\\dimen");

        parser.bind("\\hskip", [&registers, identifier, dimension](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth();
            memory::Arena& arena = parser.arena();
            const memory::Location origin = mouth.lookahead().location;
            const auto value = mouth.dimension(registers, identifier, dimension);
            const float width = value ? static_cast<float>(*value) / static_cast<float>(syntax::Number::scale) : 0.0f;

            auto* node = arena.compose<layout::Node>(layout::Node::Type::Glue);
            node->glue({ .width = width });
            return directive(arena, node, origin);
        });

        parser.bind("\\vskip", [&registers, identifier, dimension](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth();
            memory::Arena& arena = parser.arena();
            const memory::Location origin = mouth.lookahead().location;
            const auto value = mouth.dimension(registers, identifier, dimension);
            const float width = value ? static_cast<float>(*value) / static_cast<float>(syntax::Number::scale) : 0.0f;

            auto* node = arena.compose<layout::Node>(layout::Node::Type::Glue);
            node->glue({ .width = width });
            return directive(arena, node, origin);
        });

        parser.bind("\\kern", [&registers, identifier, dimension](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth();
            memory::Arena& arena = parser.arena();
            const memory::Location origin = mouth.lookahead().location;
            const auto value = mouth.dimension(registers, identifier, dimension);
            const float width = value ? static_cast<float>(*value) / static_cast<float>(syntax::Number::scale) : 0.0f;

            auto* node = arena.compose<layout::Node>(layout::Node::Type::Kern);
            node->kern({ .width = width });
            return directive(arena, node, origin);
        });

        parser.bind("\\quad", [](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena();
            const memory::Location origin = parser.mouth().lookahead().location;

            auto* node = arena.compose<layout::Node>(layout::Node::Type::Kern);
            node->kern({ .width = 12.0f });
            return directive(arena, node, origin);
        });

        parser.bind("\\qquad", [](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena();
            const memory::Location origin = parser.mouth().lookahead().location;

            auto* node = arena.compose<layout::Node>(layout::Node::Type::Kern);
            node->kern({ .width = 24.0f });
            return directive(arena, node, origin);
        });

        parser.bind("\\hfil", [](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena();
            const memory::Location origin = parser.mouth().lookahead().location;

            auto* node = arena.compose<layout::Node>(layout::Node::Type::Glue);
            node->glue({ .width = 0.0f, .stretch = 1.0f, .expand = layout::Node::Order::Fil });
            return directive(arena, node, origin);
        });

        parser.bind("\\hfill", [](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena();
            const memory::Location origin = parser.mouth().lookahead().location;

            auto* node = arena.compose<layout::Node>(layout::Node::Type::Glue);
            node->glue({ .width = 0.0f, .stretch = 1.0f, .expand = layout::Node::Order::Fill });
            return directive(arena, node, origin);
        });

        parser.bind("\\vfil", [](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena();
            const memory::Location origin = parser.mouth().lookahead().location;

            auto* node = arena.compose<layout::Node>(layout::Node::Type::Glue);
            node->glue({ .width = 0.0f, .stretch = 1.0f, .expand = layout::Node::Order::Fil });
            return directive(arena, node, origin);
        });

        parser.bind("\\vfill", [](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena();
            const memory::Location origin = parser.mouth().lookahead().location;

            auto* node = arena.compose<layout::Node>(layout::Node::Type::Glue);
            node->glue({ .width = 0.0f, .stretch = 1.0f, .expand = layout::Node::Order::Fill });
            return directive(arena, node, origin);
        });
    }

}