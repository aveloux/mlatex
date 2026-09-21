#pragma once

#include "render/primitives/configuration/typeface.hpp"
#include "render/layout/typesetter.hpp"
#include "syntax/parser.hpp"
#include "typography/shaper.hpp"

namespace render::primitives::structure {

    void alignment(
        syntax::Parser& parser,
        const typography::Shaper& shaper,
        const layout::Typesetter& typesetter,
        const configuration::Selection& selection
    );

}