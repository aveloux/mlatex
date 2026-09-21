#include "render/primitives/structure/content.hpp"

namespace render::primitives::structure {

    void append(
        std::vector<layout::Node*>& nodes,
        const syntax::Node* child,
        const typography::Shaper& shaper,
        const typography::Font* font,
        const layout::Typesetter& typesetter
    ) {
        if (!child) return;

        if (child->type == syntax::Node::Type::Text && font) {
            const typography::Font* fonts[] = { font };
            const memory::Slice<layout::Node*> shaped = shaper.shape(memory::Slice{fonts, 1}, child->value, {});
            for (std::size_t index = 0; index < shaped.size(); ++index) {
                nodes.push_back(shaped[index]);
            }
            return;
        }

        if (child->type == syntax::Node::Type::Directive && child->directive) {
            nodes.push_back(static_cast<layout::Node*>(child->directive));
            return;
        }

        if (child->type == syntax::Node::Type::Expression && child->expression && font) {
            if (layout::Node* lowered = typesetter.lower(child->expression, *font, 0.0f)) {
                nodes.push_back(lowered);
            }
        }
    }

}