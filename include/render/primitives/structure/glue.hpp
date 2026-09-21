#pragma once

#include "syntax/parser.hpp"

namespace render::primitives::structure {
    void glue(syntax::Parser& parser, syntax::semantics::Registers& registers);
}