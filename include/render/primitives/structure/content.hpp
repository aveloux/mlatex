#pragma once

#include "render/layout/node.hpp"
#include "render/layout/typesetter.hpp"
#include "syntax/node.hpp"
#include "typography/shaper.hpp"

#include <vector>

namespace render::primitives::structure {

    void append(
        std::vector<layout::Node*>& nodes,
        const syntax::Node* child,
        const typography::Shaper& shaper,
        const typography::Font* font,
        const layout::Typesetter& typesetter
    );

}