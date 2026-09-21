#pragma once

#include "render/primitives/configuration/typeface.hpp"
#include "render/layout/typesetter.hpp"
#include "syntax/parser.hpp"
#include "syntax/semantics/registers.hpp"
#include "typography/shaper.hpp"

namespace render::primitives::structure {

    void boxes(
        syntax::Parser& parser,
        syntax::semantics::Registers& registers,
        const typography::Shaper& shaper,
        const layout::Typesetter& typesetter,
        const configuration::Selection& selection
    );

}